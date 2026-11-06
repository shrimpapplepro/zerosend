//! The wire between the daemon and the ZeroSend GUI: one JSON object per
//! line. Events go to stdout, commands come in on stdin. Everything else the
//! daemon prints (tracing, panics) goes to stderr, so stdout stays parseable.

use serde::Deserialize;
use serde_json::Value;
use std::io::{BufRead, Write};
use tokio::sync::mpsc;

/// Writes one event line. stdout is locked for the whole line, so lines from
/// different tasks never interleave.
pub fn emit(event: Value) {
    let mut out = std::io::stdout().lock();
    let _ = serde_json::to_writer(&mut out, &event);
    let _ = out.write_all(b"\n");
    let _ = out.flush();
}

/// A command from the GUI.
#[derive(Debug, Deserialize)]
#[serde(tag = "cmd", rename_all = "snake_case")]
pub enum Command {
    /// Announce again and re-probe, for the GUI's refresh key.
    Refresh,
    /// Send a text message to the device with this fingerprint.
    SendText { fp: String, text: String },
    /// Send files or directories to the device with this fingerprint.
    SendFiles { fp: String, paths: Vec<String> },
    /// Answer the pending incoming request.
    Answer {
        accept: bool,
        #[serde(default)]
        pair: bool,
    },
    /// Cancel the pending request and any active transfer.
    Cancel,
    /// Forget a paired device.
    Unpair { fp: String },
    /// Re-emit the device list.
    Devices,
    /// Accept every incoming request without asking, like LocalSend's Quick Save.
    SetQuickSave { on: bool },
    Quit,
}

/// Reads commands from stdin on a blocking thread. End of input (the GUI
/// exited, or closed its pipe) is delivered as [Command::Quit], so the daemon
/// can never outlive its GUI.
pub fn spawn_reader(tx: mpsc::Sender<Command>) {
    std::thread::spawn(move || {
        let stdin = std::io::stdin();
        for line in stdin.lock().lines() {
            let Ok(line) = line else { break };
            let line = line.trim();
            if line.is_empty() {
                continue;
            }
            match serde_json::from_str::<Command>(line) {
                Ok(command) => {
                    if tx.blocking_send(command).is_err() {
                        return;
                    }
                }
                Err(err) => emit(serde_json::json!({
                    "ev": "error",
                    "text": format!("Bad command: {err}"),
                })),
            }
        }
        let _ = tx.blocking_send(Command::Quit);
    });
}
