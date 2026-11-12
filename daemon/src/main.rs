//! zerosend-daemon: a headless LocalSend peer for the ZeroSend GUI.
//!
//! The protocol work -- multicast discovery, the HTTPS server, pinned mTLS
//! uploads -- is LocalSend's own Rust core, so the CardputerZero is a full
//! LocalSend device, not an approximation of one. The session logic is forked
//! from the LocalSend CLI with its terminal UI replaced by the JSON-lines wire
//! in [ipc]: the GUI spawns this process, writes commands to its stdin and
//! reads events from its stdout.
//!
//! Usage: zerosend-daemon [--alias NAME] [--port N] [--dest DIR] [--config-dir DIR]

mod identity;
mod ipc;
mod paired;
mod sanitize;
mod send_task;
mod util;

use bytes::Bytes;
use identity::Identity;
use ipc::{Command, emit};
use localsend::discovery::{
    DEFAULT_DISCOVERY_TIMEOUT, DeviceChannel, DeviceIdentity, DiscoveredDevice, DiscoveryConfig,
    DiscoveryEvent, DiscoveryHandle, HttpChannel, StatefulDevice,
};
use localsend::http::client::v2::LsHttpClientV2;
use localsend::http::dto_v2::RegisterDtoV2;
use localsend::http::server::common::save::FileUploadTarget;
use localsend::http::server::v2::{PrepareUploadDecisionV2, ServerEventV2, SessionEndReasonV2};
use localsend::http::server::web::WebConfig;
use localsend::http::server::{ServerConfigV2, ServerHandle, start_with_port};
use localsend::model::discovery::{DeviceType, ProtocolType};
use localsend::model::transfer::{FileDto, FileMetadata};
use localsend::multicast::{DEFAULT_MULTICAST_GROUP, DEFAULT_MULTICAST_GROUP_V6, DEFAULT_PORT};
use localsend::util::interface::{InterfaceFilter, local_interface_addresses};
use paired::{PairedChannel, PairedDevices};
use send_task::{FileSource, SendCancel};
use serde_json::{Value, json};
use std::collections::{HashMap, HashSet};
use std::path::{Component, Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant};
use tokio::sync::{mpsc, oneshot};
use uuid::Uuid;
use walkdir::WalkDir;

/// Largest text sent as a message (embedded in the request as a preview).
/// Longer text is sent as a `.txt` file instead.
const MAX_MESSAGE_BYTES: usize = 64_000;

/// A device unseen for this long leaves the list. Live peers re-confirm at
/// least every 30s: each of our announcements makes them register back.
const DEVICE_STALE_AFTER: Duration = Duration::from_secs(95);

/// How often the network interfaces are re-read. LocalSend's core binds one
/// multicast socket per interface when discovery starts and never rebinds, so
/// a Wi-Fi that comes up, drops or changes address after launch would leave
/// discovery deaf (and the shown IP stale) until the app restarted.
const NETWORK_CHECK_EVERY: Duration = Duration::from_secs(3);

/// Files listed in an incoming request event; the count and total cover the rest.
const MAX_LISTED_FILES: usize = 20;

/// Everything that can happen to the daemon.
pub enum AppEvent {
    Command(Command),
    Server(ServerEventV2),
    Discovery(DiscoveryEvent),
    Tick,
    ReceiveFileResult {
        session_id: String,
        file_id: String,
        result: Result<(), String>,
    },
    SendSessionStarted {
        session_id: String,
        accepted_bytes: u64,
    },
    SendEnded {
        success: bool,
    },
    DiscoveryFinished,
    /// A fresh discovery, bound to the interfaces of `ips`, replacing the old one.
    DiscoveryRestarted {
        handle: Arc<DiscoveryHandle>,
        stop: oneshot::Sender<()>,
        ips: Vec<String>,
    },
    Log {
        text: String,
    },
}

struct Options {
    alias: Option<String>,
    port: u16,
    destination: PathBuf,
    config_dir: PathBuf,
    quick_save: bool,
}

fn parse_options() -> anyhow::Result<Options> {
    let home = dirs::home_dir().unwrap_or_else(|| PathBuf::from("."));
    let mut options = Options {
        alias: None,
        port: DEFAULT_PORT,
        destination: home.join("Downloads"),
        config_dir: std::env::var_os("XDG_CONFIG_HOME")
            .map(PathBuf::from)
            .filter(|path| path.is_absolute())
            .unwrap_or_else(|| home.join(".config"))
            .join("zerosend"),
        quick_save: false,
    };
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        let mut value = || {
            args.next()
                .ok_or_else(|| anyhow::anyhow!("{arg} needs a value"))
        };
        match arg.as_str() {
            "--alias" => options.alias = Some(value()?).filter(|alias| !alias.trim().is_empty()),
            "--port" => options.port = value()?.parse()?,
            "--dest" => options.destination = expand_tilde(PathBuf::from(value()?)),
            "--config-dir" => options.config_dir = expand_tilde(PathBuf::from(value()?)),
            "--quick-save" => options.quick_save = true,
            "--help" | "-h" => {
                eprintln!(
                    "zerosend-daemon [--alias NAME] [--port N] [--dest DIR] [--config-dir DIR] [--quick-save]"
                );
                std::process::exit(0);
            }
            other => anyhow::bail!("Unknown argument: {other}"),
        }
    }
    Ok(options)
}

fn expand_tilde(path: PathBuf) -> PathBuf {
    match (path.strip_prefix("~"), dirs::home_dir()) {
        (Ok(rest), Some(home)) => home.join(rest),
        _ => path,
    }
}

fn default_alias() -> String {
    gethostname::gethostname()
        .to_string_lossy()
        .trim_end_matches(".local")
        .to_string()
}

fn main() {
    let result = (|| -> anyhow::Result<()> {
        let options = parse_options()?;
        tokio::runtime::Runtime::new()?.block_on(run(options))
    })();
    if let Err(err) = result {
        emit(json!({ "ev": "fatal", "text": format!("{err:#}") }));
        std::process::exit(1);
    }
}

/// An incoming transfer request waiting for the user's decision.
struct PendingReceive {
    session_id: String,
    alias: String,
    sender: SenderTarget,
    files: HashMap<String, FileDto>,
    decision_tx: oneshot::Sender<PrepareUploadDecisionV2>,
}

/// Where to reach the sender of a receive session, so a receiver-side cancel
/// can be delivered back to it.
#[derive(Clone)]
struct SenderTarget {
    host: String,
    port: u16,
    protocol: ProtocolType,
    fingerprint: String,
}

/// An accepted upload session that is being received.
struct ReceiveSession {
    session_id: String,
    alias: String,
    sender: SenderTarget,
    files: HashMap<String, FileDto>,
    total_bytes: u64,
    finished_files: usize,
    failed_files: usize,
    finalized_bytes: u64,
    in_progress: HashMap<String, Arc<AtomicU64>>,
    saved: Vec<String>,
    started: Instant,
    speed: util::SpeedMeter,
    last_reported: Option<u64>,
    ended: Option<SessionEndReasonV2>,
}

impl ReceiveSession {
    fn new(
        session_id: String,
        alias: String,
        sender: SenderTarget,
        files: HashMap<String, FileDto>,
    ) -> Self {
        let total_bytes = files.values().map(|file| file.size).sum();
        Self {
            session_id,
            alias,
            sender,
            files,
            total_bytes,
            finished_files: 0,
            failed_files: 0,
            finalized_bytes: 0,
            in_progress: HashMap::new(),
            saved: Vec::new(),
            started: Instant::now(),
            speed: util::SpeedMeter::new(),
            last_reported: None,
            ended: None,
        }
    }

    fn done_bytes(&self) -> u64 {
        self.finalized_bytes
            + self
                .in_progress
                .values()
                .map(|progress| progress.load(Ordering::Relaxed))
                .sum::<u64>()
    }
}

/// An outgoing transfer driven by the send task.
struct SendState {
    session_id: Option<String>,
    alias: String,
    host: String,
    total_bytes: u64,
    sent: Arc<AtomicU64>,
    cancel: SendCancel,
    speed: util::SpeedMeter,
    last_reported: Option<u64>,
    last_log: Option<String>,
}

struct App {
    identity: Arc<Identity>,
    destination: PathBuf,
    paired: PairedDevices,
    server: Arc<ServerHandle>,
    discovery: Arc<DiscoveryHandle>,
    pending: Option<PendingReceive>,
    receive: Option<ReceiveSession>,
    send: Option<SendState>,
    discovering: bool,
    quick_save: bool,
    /// When each device last proved it is there (announcement, register,
    /// request). The core's store never forgets a device; the list does.
    last_seen: HashMap<String, Instant>,
    last_prune: Instant,
    last_devices: String,
    events_tx: mpsc::Sender<AppEvent>,
    discovery_tx: mpsc::Sender<DiscoveryEvent>,
    discovery_stop: Option<oneshot::Sender<()>>,
    /// The interface addresses last reported to the GUI.
    ips: Vec<String>,
    /// The addresses the running discovery was bound to.
    discovery_ips: Vec<String>,
    restarting_discovery: bool,
    last_network_check: Instant,
}

/// This device's addresses in the system's interface order, which the GUI
/// relies on: it shows the first one (the LAN, not a VPN tunnel).
fn interface_ips() -> Vec<String> {
    local_interface_addresses(&InterfaceFilter::default())
        .unwrap_or_default()
        .iter()
        .map(|ip| ip.to_string())
        .collect()
}

/// Whether two readings hold the same addresses, in any order.
fn same_ips(a: &[String], b: &[String]) -> bool {
    let mut a = a.to_vec();
    let mut b = b.to_vec();
    a.sort();
    b.sort();
    a == b
}

/// Starts LocalSend discovery on the interfaces that exist right now.
async fn start_discovery(
    identity: &Identity,
    event_tx: mpsc::Sender<DiscoveryEvent>,
) -> (Arc<DiscoveryHandle>, oneshot::Sender<()>) {
    let (stop_tx, stop_rx) = oneshot::channel::<()>();
    let handle = localsend::discovery::start(
        DiscoveryConfig {
            group: DEFAULT_MULTICAST_GROUP,
            group_v6: Some(DEFAULT_MULTICAST_GROUP_V6),
            port: DEFAULT_PORT,
            interface_filter: InterfaceFilter::default(),
            device: identity.multicast_device(),
            identity: DeviceIdentity {
                cert_pem: identity.cert_pem.clone(),
                private_key_pem: identity.key_pem.clone(),
            },
            timeout: DEFAULT_DISCOVERY_TIMEOUT,
            event_tx: Some(event_tx),
        },
        stop_rx,
    )
    .await;
    (Arc::new(handle), stop_tx)
}

async fn run(options: Options) -> anyhow::Result<()> {
    std::fs::create_dir_all(&options.config_dir)?;
    std::fs::create_dir_all(&options.destination)?;

    emit(json!({ "ev": "starting" }));
    let alias = options.alias.clone().unwrap_or_else(default_alias);
    let identity = {
        let dir = options.config_dir.clone();
        let port = options.port;
        Arc::new(
            tokio::task::spawn_blocking(move || Identity::load_or_generate(&dir, alias, port))
                .await??,
        )
    };
    let paired = PairedDevices::load(&options.config_dir)?;

    let (events_tx, mut events_rx) = mpsc::channel::<AppEvent>(64);
    let (server_tx, mut server_rx) = mpsc::channel::<ServerEventV2>(16);
    let (server_stop_tx, server_stop_rx) = oneshot::channel::<()>();
    let server = Arc::new(
        start_with_port(
            identity.port,
            Some(identity.tls_config()),
            identity.client_info(),
            None,
            Some(ServerConfigV2 {
                pin: None,
                verify_checksums: true,
                event_tx: server_tx,
            }),
            WebConfig::default(),
            server_stop_rx,
        )
        .await?,
    );

    let (discovery_tx, mut discovery_rx) = mpsc::channel::<DiscoveryEvent>(16);
    let ips = interface_ips();
    let (discovery, discovery_stop_tx) = start_discovery(&identity, discovery_tx.clone()).await;

    let (command_tx, mut command_rx) = mpsc::channel::<Command>(16);
    ipc::spawn_reader(command_tx);

    emit(json!({
        "ev": "ready",
        "alias": identity.alias,
        "port": identity.port,
        "fingerprint": identity.fingerprint,
        "dest": options.destination.display().to_string(),
        "ips": ips,
        "multicast": discovery.multicast_error().is_none(),
        "quick_save": options.quick_save,
    }));
    if let Some(err) = discovery.multicast_error() {
        emit(json!({ "ev": "log", "text": format!("Multicast unavailable: {err:#}") }));
    }

    let mut app = App {
        identity,
        destination: options.destination,
        paired,
        server,
        discovery,
        pending: None,
        receive: None,
        send: None,
        discovering: false,
        quick_save: options.quick_save,
        last_seen: HashMap::new(),
        last_prune: Instant::now(),
        last_devices: String::new(),
        events_tx,
        discovery_tx,
        discovery_stop: Some(discovery_stop_tx),
        discovery_ips: ips.clone(),
        ips,
        restarting_discovery: false,
        last_network_check: Instant::now(),
    };
    app.emit_devices(true);
    app.refresh();

    let mut tick = tokio::time::interval(Duration::from_millis(250));
    tick.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    // Re-announce now and then: phones that joined the network after us, or
    // whose app was in the background, otherwise never hear about this device.
    let mut announce = tokio::time::interval(Duration::from_secs(30));
    announce.set_missed_tick_behavior(tokio::time::MissedTickBehavior::Skip);
    announce.tick().await;

    loop {
        let event = tokio::select! {
            Some(command) = command_rx.recv() => AppEvent::Command(command),
            Some(event) = app_events(&mut events_rx) => event,
            Some(event) = server_rx.recv() => AppEvent::Server(event),
            Some(event) = discovery_rx.recv() => AppEvent::Discovery(event),
            _ = tick.tick() => AppEvent::Tick,
            _ = announce.tick() => {
                let discovery = app.discovery.clone();
                tokio::spawn(async move { discovery.announce().await });
                continue;
            }
        };
        if app.dispatch(event) {
            break;
        }
    }

    // Shutdown: decline what is pending, cancel what is running, release the ports.
    app.handle_cancel();
    let _ = server_stop_tx.send(());
    if let Some(stop) = app.discovery_stop.take() {
        let _ = stop.send(());
    }
    let _ = tokio::time::timeout(Duration::from_secs(1), app.server.wait_stopped()).await;
    let _ = tokio::time::timeout(Duration::from_secs(1), app.discovery.wait_stopped()).await;
    emit(json!({ "ev": "stopped" }));
    Ok(())
}

async fn app_events(rx: &mut mpsc::Receiver<AppEvent>) -> Option<AppEvent> {
    rx.recv().await
}

fn device_type_name(device_type: &Option<DeviceType>) -> &'static str {
    match device_type {
        Some(DeviceType::Mobile) => "mobile",
        Some(DeviceType::Desktop) => "desktop",
        Some(DeviceType::Web) => "web",
        Some(DeviceType::Headless) => "headless",
        Some(DeviceType::Server) => "server",
        None => "unknown",
    }
}

/// Returns the text of a "message request": a single text file whose content
/// is already included as a preview, so there is nothing left to transfer.
fn message_of(files: &HashMap<String, FileDto>) -> Option<&str> {
    if files.len() != 1 {
        return None;
    }
    let file = files.values().next()?;
    if file.file_type != "text" && !file.file_type.starts_with("text/") {
        return None;
    }
    file.preview.as_deref()
}

impl App {
    /// Handles one event; returns true when the daemon should exit.
    fn dispatch(&mut self, event: AppEvent) -> bool {
        match event {
            AppEvent::Command(command) => return self.handle_command(command),
            AppEvent::Server(event) => self.handle_server_event(event),
            AppEvent::Discovery(event) => self.handle_discovery(event),
            AppEvent::Tick => {
                self.report_progress();
                if self.last_prune.elapsed() > Duration::from_secs(5) {
                    self.last_prune = Instant::now();
                    self.emit_devices(false); // drops devices that went quiet
                }
                if self.last_network_check.elapsed() > NETWORK_CHECK_EVERY {
                    self.last_network_check = Instant::now();
                    self.watch_network();
                }
            }
            AppEvent::ReceiveFileResult {
                session_id,
                file_id,
                result,
            } => self.receive_file_result(session_id, file_id, result),
            AppEvent::SendSessionStarted {
                session_id,
                accepted_bytes,
            } => {
                if let Some(send) = &mut self.send {
                    send.session_id = Some(session_id);
                    send.total_bytes = accepted_bytes;
                    emit(json!({
                        "ev": "send_accepted",
                        "alias": send.alias,
                        "total": accepted_bytes,
                    }));
                }
            }
            AppEvent::SendEnded { success } => {
                if let Some(send) = self.send.take() {
                    emit(json!({
                        "ev": "send_done",
                        "ok": success,
                        "alias": send.alias,
                        "text": send.last_log.unwrap_or_else(|| match success {
                            true => "Sent".to_string(),
                            false => "Failed".to_string(),
                        }),
                    }));
                }
            }
            AppEvent::DiscoveryRestarted { handle, stop, ips } => {
                self.discovery_restarted(handle, stop, ips)
            }
            AppEvent::DiscoveryFinished => {
                self.discovering = false;
                emit(json!({ "ev": "scan", "active": false }));
                self.emit_devices(true);
            }
            AppEvent::Log { text } => {
                if let Some(send) = &mut self.send {
                    send.last_log = Some(text.clone());
                }
                emit(json!({ "ev": "log", "text": text }));
            }
        }
        false
    }

    fn handle_command(&mut self, command: Command) -> bool {
        match command {
            Command::Quit => return true,
            Command::Refresh => self.refresh(),
            Command::Devices => self.emit_devices(true),
            Command::SetQuickSave { on } => self.quick_save = on,
            Command::SendText { fp, text } => {
                if text.is_empty() {
                    return false;
                }
                let transfer = text_transfer(Bytes::from(text.into_bytes()));
                self.start_send(&fp, transfer);
            }
            Command::SendFiles { fp, paths } => {
                let (files, sources, total, skipped) =
                    collect_files(paths.into_iter().map(PathBuf::from).collect());
                for path in skipped {
                    emit(json!({ "ev": "log", "text": format!("Skipped {path}") }));
                }
                if files.is_empty() {
                    emit(json!({ "ev": "send_done", "ok": false, "alias": "", "text": "Nothing readable to send" }));
                    return false;
                }
                let sources = sources
                    .into_iter()
                    .map(|(id, path)| (id, FileSource::Path(path)))
                    .collect();
                self.start_send(&fp, (files, sources, total));
            }
            Command::Answer { accept, pair } => self.answer_pending(accept, pair),
            Command::Cancel => self.handle_cancel(),
            Command::Unpair { fp } => {
                match self.paired.remove(&fp) {
                    Ok(_) => {}
                    Err(err) => emit(json!({ "ev": "log", "text": format!("Unpair not saved: {err:#}") })),
                }
                self.emit_devices(true);
            }
        }
        false
    }

    /// Re-reads the interfaces. A change is reported to the GUI at once (an
    /// empty list means no network), and discovery is restarted on the new
    /// interfaces, one restart at a time.
    fn watch_network(&mut self) {
        let ips = interface_ips();
        if !same_ips(&ips, &self.ips) {
            self.ips = ips.clone();
            emit(json!({
                "ev": "network",
                "ips": ips,
                "multicast": !ips.is_empty() && self.discovery.multicast_error().is_none(),
            }));
        }
        if self.restarting_discovery || same_ips(&ips, &self.discovery_ips) {
            return;
        }
        self.restarting_discovery = true;
        let old = self.discovery.clone();
        let old_stop = self.discovery_stop.take();
        let identity = self.identity.clone();
        let discovery_tx = self.discovery_tx.clone();
        let events_tx = self.events_tx.clone();
        tokio::spawn(async move {
            // The new sockets bind the same port, so the old ones go first.
            if let Some(stop) = old_stop {
                let _ = stop.send(());
            }
            let _ = tokio::time::timeout(Duration::from_secs(2), old.wait_stopped()).await;
            let (handle, stop) = start_discovery(&identity, discovery_tx).await;
            let _ = events_tx.send(AppEvent::DiscoveryRestarted { handle, stop, ips }).await;
        });
    }

    fn discovery_restarted(
        &mut self,
        handle: Arc<DiscoveryHandle>,
        stop: oneshot::Sender<()>,
        ips: Vec<String>,
    ) {
        self.discovery = handle;
        self.discovery_stop = Some(stop);
        self.discovery_ips = ips;
        self.restarting_discovery = false;
        let multicast = self.discovery.multicast_error().is_none();
        emit(json!({ "ev": "network", "ips": self.ips, "multicast": multicast }));
        if self.ips.is_empty() {
            return; // nothing to announce on; the next change restarts again
        }
        if let Some(err) = self.discovery.multicast_error() {
            emit(json!({ "ev": "log", "text": format!("Multicast unavailable: {err:#}") }));
        }
        let discovery = self.discovery.clone();
        tokio::spawn(async move { discovery.announce().await });
        self.refresh();
    }

    /// Announces this device, probes the paired devices' last addresses, and
    /// scans the local subnets when that finds nothing.
    fn refresh(&mut self) {
        if self.discovering {
            return;
        }
        self.discovering = true;
        emit(json!({ "ev": "scan", "active": true }));
        let discovery = self.discovery.clone();
        let events_tx = self.events_tx.clone();
        let known = self.paired.known_http_channels();
        let port = self.identity.port;
        let interface_ips =
            local_interface_addresses(&InterfaceFilter::default()).unwrap_or_default();
        tokio::spawn(async move {
            let result = discovery
                .discover_staged(
                    known,
                    interface_ips,
                    port,
                    ProtocolType::Https,
                    Duration::from_secs(1),
                )
                .await;
            if let Err(err) = result {
                let _ = events_tx
                    .send(AppEvent::Log {
                        text: format!("Discovery failed: {err}"),
                    })
                    .await;
            }
            let _ = events_tx.send(AppEvent::DiscoveryFinished).await;
        });
    }

    /// Emits the device list: paired devices first, then by name. Skipped
    /// when nothing changed, unless `force`.
    fn emit_devices(&mut self, force: bool) {
        let mut list: Vec<Value> = self
            .discovery
            .devices()
            .iter()
            .filter(|stored| stored.device.fingerprint != self.identity.fingerprint)
            .filter(|stored| {
                self.last_seen
                    .get(&stored.device.fingerprint)
                    .is_some_and(|seen| seen.elapsed() < DEVICE_STALE_AFTER)
            })
            .map(|stored| {
                // Shown to the user, so prefer an IPv4 address: the best
                // channel flips between link-local IPv6 addresses on every
                // announcement, which would also re-emit the list each time.
                let channels = stored.get_ranked_channels();
                let http = channels
                    .iter()
                    .filter_map(|channel| channel.http())
                    .find(|http| !http.host.contains(':'))
                    .or_else(|| channels.first().and_then(|channel| channel.http()));
                json!({
                    "fp": stored.device.fingerprint,
                    "alias": sanitize::single_line(&stored.device.alias),
                    "model": stored.device.device_model.as_deref().map(sanitize::single_line),
                    "type": device_type_name(&stored.device.device_type),
                    "host": http.map(|http| http.host.clone()),
                    "port": http.map(|http| http.port),
                    "https": http.map(|http| http.protocol == ProtocolType::Https),
                    "paired": self.paired.contains(&stored.device.fingerprint),
                })
            })
            .collect();
        list.sort_by(|a, b| {
            let key = |v: &Value| {
                (
                    !v["paired"].as_bool().unwrap_or(false),
                    v["alias"].as_str().unwrap_or("").to_lowercase(),
                )
            };
            key(a).cmp(&key(b))
        });
        let event = json!({ "ev": "devices", "list": list });
        let text = event.to_string();
        if force || text != self.last_devices {
            self.last_devices = text;
            emit(event);
        }
    }

    fn handle_discovery(&mut self, event: DiscoveryEvent) {
        let device = match &event {
            DiscoveryEvent::Discovered { device } | DiscoveryEvent::Updated { device } => device,
            DiscoveryEvent::MulticastFailed => {
                emit(json!({ "ev": "log", "text": "Multicast stopped; no longer hearing announcements" }));
                return;
            }
        };
        let fingerprint = device.fingerprint.clone();
        self.last_seen.insert(fingerprint.clone(), Instant::now());
        if self.paired.contains(&fingerprint)
            && let Some(stored) = self.discovery.device_by_fingerprint(&fingerprint)
        {
            let _ = self.paired.update(
                &fingerprint,
                &sanitize::single_line(&stored.device.alias),
                PairedChannel::channels_of(&stored),
            );
        }
        self.emit_devices(false);
    }

    /// Adds a device that contacted this one over HTTP to the store.
    fn device_confirmed(&mut self, host: String, info: RegisterDtoV2) {
        if info.fingerprint == self.identity.fingerprint {
            return;
        }
        self.last_seen.insert(info.fingerprint.clone(), Instant::now());
        let device = DiscoveredDevice {
            alias: info.alias,
            version: info.version,
            device_model: info.device_model,
            device_type: info.device_type,
            fingerprint: info.fingerprint,
            channel: DeviceChannel::Http(HttpChannel {
                host,
                port: info.port,
                protocol: info.protocol,
            }),
            download: info.download,
        };
        let discovery = self.discovery.clone();
        tokio::spawn(async move {
            discovery.add_device(device).await;
        });
    }

    fn handle_server_event(&mut self, event: ServerEventV2) {
        match event {
            ServerEventV2::Register { ip, info } => self.device_confirmed(ip.to_string(), info),
            ServerEventV2::PrepareUpload {
                session_id,
                ip,
                info,
                cert_fingerprint,
                files,
                decision_tx,
            } => {
                self.device_confirmed(ip.to_string(), info.clone());
                let alias = sanitize::single_line(&info.alias);

                if let Some(message) = message_of(&files) {
                    // The text is the whole request: accepting no file ends it.
                    let _ = decision_tx.send(PrepareUploadDecisionV2::Accept(HashSet::new()));
                    emit(json!({
                        "ev": "message",
                        "alias": alias,
                        "text": sanitize::multi_line(message),
                    }));
                    return;
                }

                if self.pending.is_some() {
                    let _ = decision_tx.send(PrepareUploadDecisionV2::Decline);
                    return;
                }

                let sender = SenderTarget {
                    host: ip.to_string(),
                    port: info.port,
                    protocol: info.protocol,
                    fingerprint: cert_fingerprint.unwrap_or_else(|| info.fingerprint.clone()),
                };
                let total: u64 = files.values().map(|file| file.size).sum();

                if self.quick_save || self.paired.contains(&sender.fingerprint) {
                    let ids: HashSet<String> = files.keys().cloned().collect();
                    if decision_tx.send(PrepareUploadDecisionV2::Accept(ids)).is_ok() {
                        emit(json!({
                            "ev": "recv_start",
                            "alias": alias,
                            "count": files.len(),
                            "total": total,
                            "auto": true,
                        }));
                        self.receive = Some(ReceiveSession::new(session_id, alias, sender, files));
                    }
                    return;
                }

                let mut sorted: Vec<&FileDto> = files.values().collect();
                sorted.sort_by_key(|file| std::cmp::Reverse(file.size));
                let listed: Vec<Value> = sorted
                    .iter()
                    .take(MAX_LISTED_FILES)
                    .map(|file| {
                        json!({
                            "name": sanitize::single_line(&file.file_name),
                            "size": file.size,
                            "type": file.file_type,
                        })
                    })
                    .collect();
                emit(json!({
                    "ev": "request",
                    "alias": alias,
                    "model": info.device_model.as_deref().map(sanitize::single_line),
                    "type": device_type_name(&info.device_type),
                    "count": files.len(),
                    "total": total,
                    "files": listed,
                }));
                self.pending = Some(PendingReceive {
                    session_id,
                    alias,
                    sender,
                    files,
                    decision_tx,
                });
            }
            ServerEventV2::FileUpload {
                session_id,
                file_id,
                file,
                target_tx,
            } => self.handle_file_upload(session_id, file_id, file, target_tx),
            ServerEventV2::SessionEnd { session_id, reason } => {
                let Some(session) = self
                    .receive
                    .as_mut()
                    .filter(|session| session.session_id == session_id)
                else {
                    return;
                };
                session.ended = Some(reason);
                self.finish_receive_if_done();
            }
            ServerEventV2::PrepareUploadAborted { session_id } => {
                if self
                    .pending
                    .as_ref()
                    .is_some_and(|pending| pending.session_id == session_id)
                {
                    let pending = self.pending.take().unwrap();
                    emit(json!({ "ev": "request_ended", "alias": pending.alias, "text": "Cancelled by sender" }));
                }
            }
            ServerEventV2::CancelReceived { ip, session_id } => {
                if let Some(send) = &self.send
                    && send.session_id.as_deref() == Some(session_id.as_str())
                    && send.host == ip.to_string()
                {
                    send.cancel.by_peer.store(true, Ordering::Relaxed);
                    send.cancel.token.cancel();
                }
            }
            ServerEventV2::ListenerFailed { error } => {
                emit(json!({ "ev": "error", "text": format!("Server stopped, no longer receiving: {error}") }));
            }
        }
    }

    fn handle_file_upload(
        &mut self,
        session_id: String,
        file_id: String,
        file: FileDto,
        target_tx: oneshot::Sender<FileUploadTarget>,
    ) {
        let Some(session) = self
            .receive
            .as_mut()
            .filter(|session| session.session_id == session_id)
        else {
            return; // Unknown session: dropping the responder fails the request.
        };

        let path = util::unique_path(&self.destination, &file.file_name);
        if let Some(parent) = path.parent() {
            let _ = std::fs::create_dir_all(parent);
        }
        session.saved.push(path.display().to_string());

        let progress = Arc::new(AtomicU64::new(0));
        session.in_progress.insert(file_id.clone(), progress.clone());

        let (progress_tx, mut progress_rx) = mpsc::channel::<u64>(16);
        tokio::spawn(async move {
            while let Some(written) = progress_rx.recv().await {
                progress.store(written, Ordering::Relaxed);
            }
        });

        let (result_tx, result_rx) = oneshot::channel::<Result<(), String>>();
        let events_tx = self.events_tx.clone();
        tokio::spawn(async move {
            let result = result_rx
                .await
                .unwrap_or_else(|_| Err("Upload aborted".to_string()));
            let _ = events_tx
                .send(AppEvent::ReceiveFileResult {
                    session_id,
                    file_id,
                    result,
                })
                .await;
        });

        let _ = target_tx.send(FileUploadTarget::Path {
            path,
            result_tx,
            progress_tx: Some(progress_tx),
        });
    }

    fn receive_file_result(&mut self, session_id: String, file_id: String, result: Result<(), String>) {
        let Some(session) = self
            .receive
            .as_mut()
            .filter(|session| session.session_id == session_id)
        else {
            return;
        };
        session.in_progress.remove(&file_id);
        match result {
            Ok(()) => {
                session.finished_files += 1;
                session.finalized_bytes += session.files.get(&file_id).map(|f| f.size).unwrap_or(0);
            }
            Err(err) => {
                session.failed_files += 1;
                let name = session
                    .files
                    .get(&file_id)
                    .map(|file| sanitize::single_line(&file.file_name))
                    .unwrap_or_else(|| file_id.clone());
                emit(json!({ "ev": "log", "text": format!("Failed to receive {name}: {err}") }));
            }
        }
        self.finish_receive_if_done();
    }

    /// Reports the end of the receive session once the server said it ended
    /// and no per-file result is still outstanding.
    fn finish_receive_if_done(&mut self) {
        let done = self
            .receive
            .as_ref()
            .is_some_and(|session| session.ended.is_some() && session.in_progress.is_empty());
        if !done {
            return;
        }
        let session = self.receive.take().unwrap();
        let cancelled = matches!(session.ended, Some(SessionEndReasonV2::Cancelled));
        let text = match cancelled {
            false => format!(
                "Received {} file{} ({}, {})",
                session.finished_files,
                if session.finished_files == 1 { "" } else { "s" },
                util::format_bytes(session.finalized_bytes),
                util::format_duration(session.started.elapsed()),
            ),
            true => format!(
                "Cancelled by sender ({} of {} received)",
                session.finished_files,
                session.files.len()
            ),
        };
        emit(json!({
            "ev": "recv_done",
            "ok": !cancelled && session.failed_files == 0,
            "alias": session.alias,
            "files": session.finished_files,
            "failed": session.failed_files,
            "bytes": session.finalized_bytes,
            "cancelled": cancelled,
            "text": text,
            "paths": session.saved,
        }));
    }

    fn answer_pending(&mut self, accept: bool, pair: bool) {
        let Some(pending) = self.pending.take() else {
            return;
        };
        if !accept {
            let _ = pending.decision_tx.send(PrepareUploadDecisionV2::Decline);
            emit(json!({ "ev": "request_ended", "alias": pending.alias, "text": "Declined" }));
            return;
        }
        if pair {
            let channels = self
                .discovery
                .device_by_fingerprint(&pending.sender.fingerprint)
                .map(|stored| PairedChannel::channels_of(&stored))
                .unwrap_or_default();
            if let Err(err) = self.paired.insert(
                pending.sender.fingerprint.clone(),
                pending.alias.clone(),
                channels,
            ) {
                emit(json!({ "ev": "log", "text": format!("Paired for this run only: {err:#}") }));
            }
            self.emit_devices(true);
        }
        let ids: HashSet<String> = pending.files.keys().cloned().collect();
        if pending
            .decision_tx
            .send(PrepareUploadDecisionV2::Accept(ids))
            .is_err()
        {
            emit(json!({ "ev": "request_ended", "alias": pending.alias, "text": "Request already ended" }));
            return;
        }
        let total: u64 = pending.files.values().map(|file| file.size).sum();
        emit(json!({
            "ev": "recv_start",
            "alias": pending.alias,
            "count": pending.files.len(),
            "total": total,
            "auto": false,
        }));
        self.receive = Some(ReceiveSession::new(
            pending.session_id,
            pending.alias,
            pending.sender,
            pending.files,
        ));
    }

    /// Cancels everything in flight: the pending request, the send, the receive.
    fn handle_cancel(&mut self) {
        if self.pending.is_some() {
            self.answer_pending(false, false);
        }
        if let Some(send) = &self.send {
            send.cancel.token.cancel();
        }
        if let Some(session) = self.receive.take() {
            let server = self.server.clone();
            let identity = self.identity.clone();
            let sender = session.sender.clone();
            let session_id = session.session_id.clone();
            tokio::spawn(async move {
                server.cancel_v2_session(&session_id).await;
                // Best effort: otherwise the sender only notices by its uploads failing.
                let expected = match sender.protocol {
                    ProtocolType::Https => Some(sender.fingerprint.clone()),
                    ProtocolType::Http => None,
                };
                let Ok(client) = LsHttpClientV2::try_new(
                    &identity.key_pem,
                    &identity.cert_pem,
                    expected,
                    Some(Duration::from_secs(5)),
                ) else {
                    return;
                };
                let _ = client
                    .cancel(sender.protocol, &sender.host, sender.port, &session_id)
                    .await;
            });
            emit(json!({
                "ev": "recv_done",
                "ok": false,
                "alias": session.alias,
                "files": session.finished_files,
                "failed": session.failed_files,
                "bytes": session.finalized_bytes,
                "cancelled": true,
                "text": format!("You cancelled ({} of {} received)", session.finished_files, session.files.len()),
                "paths": session.saved,
            }));
        }
    }

    fn start_send(&mut self, fingerprint: &str, (files, sources, total_bytes): Transfer) {
        if self.send.is_some() {
            emit(json!({ "ev": "log", "text": "A send is already in progress" }));
            return;
        }
        let Some(device) = self.discovery.device_by_fingerprint(fingerprint) else {
            emit(json!({ "ev": "send_done", "ok": false, "alias": "", "text": "Device is gone" }));
            return;
        };
        let Some(host) = dialable_host(&device) else {
            emit(json!({ "ev": "send_done", "ok": false, "alias": device.device.alias, "text": "No dialable address" }));
            return;
        };
        let alias = sanitize::single_line(&device.device.alias);
        let progress = Arc::new(AtomicU64::new(0));
        let cancel = SendCancel::new();
        let is_text = files.len() == 1 && files.values().all(|file| file.preview.is_some());
        emit(json!({
            "ev": "send_waiting",
            "alias": alias,
            "count": files.len(),
            "total": total_bytes,
            "text": is_text,
        }));
        self.send = Some(SendState {
            session_id: None,
            alias,
            host,
            total_bytes,
            sent: progress.clone(),
            cancel: cancel.clone(),
            speed: util::SpeedMeter::new(),
            last_reported: None,
            last_log: None,
        });
        tokio::spawn(send_task::run_send(
            self.identity.clone(),
            device,
            files,
            sources,
            progress,
            cancel,
            self.events_tx.clone(),
        ));
    }

    fn report_progress(&mut self) {
        if let Some(send) = &mut self.send
            && send.session_id.is_some()
        {
            let done = send.sent.load(Ordering::Relaxed);
            let speed = send.speed.update(done);
            if send.last_reported != Some(done) {
                send.last_reported = Some(done);
                emit(json!({
                    "ev": "progress", "dir": "send",
                    "done": done, "total": send.total_bytes, "speed": speed as u64,
                }));
            }
        }
        if let Some(session) = &mut self.receive {
            let done = session.done_bytes();
            let speed = session.speed.update(done);
            if session.last_reported != Some(done) {
                session.last_reported = Some(done);
                emit(json!({
                    "ev": "progress", "dir": "recv",
                    "done": done, "total": session.total_bytes, "speed": speed as u64,
                }));
            }
        }
    }
}

fn dialable_host(device: &StatefulDevice) -> Option<String> {
    device
        .get_best_channel()
        .and_then(|channel| channel.http())
        .map(|http| http.host.clone())
}

/// Transfer metadata keyed by file ID, the content under the same IDs, and
/// the total byte count.
type Transfer = (HashMap<String, FileDto>, HashMap<String, FileSource>, u64);

/// A text as a message carried by the preview of a single text file, so the
/// receiver has nothing left to download. Too long, and it becomes a `.txt`.
fn text_transfer(text: Bytes) -> Transfer {
    let id = Uuid::new_v4().to_string();
    let size = text.len() as u64;
    let preview = (text.len() <= MAX_MESSAGE_BYTES)
        .then(|| String::from_utf8_lossy(&text).into_owned());
    let file = FileDto {
        id: id.clone(),
        file_name: format!("{id}.txt"),
        size,
        file_type: "text/plain".to_string(),
        sha256: None,
        preview,
        metadata: None,
    };
    (
        HashMap::from([(id.clone(), file)]),
        HashMap::from([(id, FileSource::Bytes(text))]),
        size,
    )
}

/// Expands files and directories into transfer metadata. Directory entries are
/// named with the directory's own name as their root, so the receiver rebuilds
/// it. Returns the files, their paths, the total size and what was skipped.
fn collect_files(
    picked: Vec<PathBuf>,
) -> (HashMap<String, FileDto>, HashMap<String, PathBuf>, u64, Vec<String>) {
    let mut files = HashMap::new();
    let mut paths = HashMap::new();
    let mut skipped = Vec::new();
    let mut total = 0u64;
    for root in &picked {
        for entry in expand(root) {
            let (path, file_name) = match entry {
                Ok(entry) => entry,
                Err(path) => {
                    skipped.push(path.display().to_string());
                    continue;
                }
            };
            let Ok(metadata) = std::fs::metadata(&path).and_then(|m| {
                if m.is_file() { Ok(m) } else { Err(std::io::Error::other("not a file")) }
            }) else {
                skipped.push(path.display().to_string());
                continue;
            };
            let id = Uuid::new_v4().to_string();
            total += metadata.len();
            files.insert(
                id.clone(),
                FileDto {
                    id: id.clone(),
                    file_name,
                    size: metadata.len(),
                    file_type: mime_guess::from_path(&path).first_or_octet_stream().to_string(),
                    sha256: None,
                    preview: None,
                    metadata: FileMetadata::from_fs_metadata(&metadata),
                },
            );
            paths.insert(id, path);
        }
    }
    (files, paths, total, skipped)
}

fn expand(path: &Path) -> Vec<Result<(PathBuf, String), PathBuf>> {
    let Ok(metadata) = path.metadata() else {
        return vec![Err(path.to_path_buf())];
    };
    let name_of = |p: &Path| p.file_name().map(|n| n.to_string_lossy().into_owned());
    if metadata.is_file() {
        return vec![Ok((
            path.to_path_buf(),
            name_of(path).unwrap_or_else(|| "unnamed".to_string()),
        ))];
    }
    if !metadata.is_dir() {
        return vec![Err(path.to_path_buf())];
    }
    let Some(root) = name_of(path).or_else(|| path.canonicalize().ok().and_then(|p| name_of(&p)))
    else {
        return vec![Err(path.to_path_buf())];
    };
    WalkDir::new(path)
        .min_depth(1)
        .sort_by_file_name()
        .into_iter()
        .filter_map(|entry| match entry {
            Ok(entry) if entry.file_type().is_file() => {
                let relative = entry.path().strip_prefix(path).ok()?;
                let name = std::iter::once(root.clone())
                    .chain(relative.components().filter_map(|c| match c {
                        Component::Normal(n) => Some(n.to_string_lossy().into_owned()),
                        _ => None,
                    }))
                    .collect::<Vec<_>>()
                    .join("/");
                Some(Ok((entry.path().to_path_buf(), name)))
            }
            Ok(_) => None,
            Err(err) => Some(Err(err.path().unwrap_or(path).to_path_buf())),
        })
        .collect()
}
