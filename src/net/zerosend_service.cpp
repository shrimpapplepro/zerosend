/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "zerosend_service.h"

#include "logger.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace net {
namespace {

using json = nlohmann::json;

// Bounded so a long uptime cannot grow the inbox without limit on 512MB.
constexpr std::size_t kMaxInboxItems = 50;

std::string str(const json& value, const char* key) {
    const auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}

uint64_t u64(const json& value, const char* key) {
    const auto it = value.find(key);
    return it != value.end() && it->is_number() ? it->get<uint64_t>() : 0;
}

bool flag(const json& value, const char* key, bool fallback = false) {
    const auto it = value.find(key);
    return it != value.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

// Writes all of `data`, retrying on EINTR and short writes.
bool write_all(int fd, const std::string& data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const auto n = ::write(fd, data.data() + written, data.size() - written);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        written += static_cast<std::size_t>(n);
    }
    return true;
}

} // namespace

bool load_service_settings(const std::string& path, ServiceOptions& options) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const auto key = line.substr(0, eq);
        const auto value = line.substr(eq + 1);
        if (key == "alias") {
            options.alias = value;
        } else if (key == "port") {
            const int port = std::atoi(value.c_str());
            if (port > 0 && port < 65536) {
                options.port = port;
            }
        } else if (key == "destination") {
            options.destination = value;
        } else if (key == "quick_save") {
            options.quick_save = value == "yes" || value == "true" || value == "1";
        }
    }
    return true;
}

bool save_service_settings(const std::string& path, const ServiceOptions& options) {
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    const auto temp = path + ".tmp";
    {
        std::ofstream output(temp, std::ios::trunc);
        if (!output) {
            return false;
        }
        output << "# ZeroSend settings, written by the app.\n"
               << "alias=" << options.alias << "\n"
               << "port=" << options.port << "\n"
               << "destination=" << options.destination << "\n"
               << "quick_save=" << (options.quick_save ? "yes" : "no") << "\n";
        if (!output) {
            return false;
        }
    }
    std::filesystem::rename(temp, path, error);
    return !error;
}

Service& Service::instance() {
    static Service service;
    return service;
}

Service::Service() {
    lv_subject_init_int(&revision_, 0);
}

Service::~Service() {
    stop();
}

void Service::start(const ServiceOptions& options) {
    if (pid_ > 0) {
        return;
    }
    options_ = options;
    state_ = ServiceState{};
    changed();

    // A dead daemon must not kill the GUI: writing to its closed stdin would
    // otherwise raise SIGPIPE and end this process too.
    std::signal(SIGPIPE, SIG_IGN);

    int to_child[2];
    int from_child[2];
    if (pipe(to_child) != 0 || pipe(from_child) != 0) {
        state_.status = ServiceState::Status::Failed;
        state_.status_text = std::string("pipe: ") + std::strerror(errno);
        changed();
        return;
    }

    std::vector<std::string> args = {options.daemon_path};
    if (!options.alias.empty()) {
        args.insert(args.end(), {"--alias", options.alias});
    }
    args.insert(args.end(), {"--port", std::to_string(options.port)});
    if (!options.destination.empty()) {
        args.insert(args.end(), {"--dest", options.destination});
    }
    if (options.quick_save) {
        args.push_back("--quick-save");
    }
    std::vector<char*> argv;
    for (auto& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);
    posix_spawn_file_actions_addopen(&actions,
                                     STDERR_FILENO,
                                     options.log_path.empty() ? "/dev/null" : options.log_path.c_str(),
                                     O_WRONLY | O_CREAT | O_TRUNC,
                                     0644);
    posix_spawn_file_actions_addclose(&actions, to_child[1]);
    posix_spawn_file_actions_addclose(&actions, from_child[0]);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, options.daemon_path.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(to_child[0]);
    ::close(from_child[1]);

    if (rc != 0) {
        ::close(to_child[1]);
        ::close(from_child[0]);
        state_.status = ServiceState::Status::Failed;
        state_.status_text = "Cannot start daemon: " + std::string(std::strerror(rc));
        LOG_ERROR("posix_spawn {} failed: {}", options.daemon_path, std::strerror(rc));
        changed();
        return;
    }

    LOG_INFO("started zerosend-daemon pid {} ({})", pid, options.daemon_path);
    fcntl(to_child[1], F_SETFD, FD_CLOEXEC);
    fcntl(from_child[0], F_SETFD, FD_CLOEXEC);
    pid_ = pid;
    stdin_fd_ = to_child[1];
    stopping_ = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.clear(); // lines from a previous daemon must not land on this one's state
        reader_finished_ = false;
    }
    reader_ = std::thread([this, fd = from_child[0]]() { reader_loop(fd); });

    if (!poll_timer_) {
        poll_timer_ = lv_timer_create(poll_timer_cb, 40, this);
    }
}

void Service::stop() {
    if (pid_ <= 0) {
        return;
    }
    stopping_ = true;
    write_command(R"({"cmd":"quit"})");
    if (stdin_fd_ >= 0) {
        ::close(stdin_fd_); // EOF also means quit, should the line not arrive
        stdin_fd_ = -1;
    }

    // The daemon releases its ports within ~2s; past that it is stuck.
    int status = 0;
    bool exited = false;
    for (int i = 0; i < 30 && !exited; ++i) {
        exited = waitpid(pid_, &status, WNOHANG) == pid_;
        if (!exited) {
            usleep(100 * 1000);
        }
    }
    if (!exited) {
        kill(pid_, SIGKILL);
        waitpid(pid_, &status, 0);
    }
    pid_ = -1;
    if (reader_.joinable()) {
        reader_.join();
    }
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
}

void Service::update_options(const ServiceOptions& options) {
    const bool restart = options.alias != options_.alias || options.port != options_.port ||
                         options.destination != options_.destination;
    const bool quick_save_changed = options.quick_save != options_.quick_save;
    if (!settings_path_.empty() && !save_service_settings(settings_path_, options)) {
        LOG_WARN("could not save settings to {}", settings_path_);
    }
    if (restart) {
        // Keep what the user has received across the restart.
        auto inbox = state_.inbox;
        stop();
        start(options);
        state_.inbox = std::move(inbox);
        changed();
        return;
    }
    options_ = options;
    if (quick_save_changed) {
        write_command(json{{"cmd", "set_quick_save"}, {"on", options.quick_save}}.dump());
    }
    changed();
}

void Service::reader_loop(int fd) {
    std::string buffer;
    char chunk[4096];
    while (true) {
        const auto n = ::read(fd, chunk, sizeof(chunk));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        buffer.append(chunk, static_cast<std::size_t>(n));
        std::size_t newline;
        while ((newline = buffer.find('\n')) != std::string::npos) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.push_back(buffer.substr(0, newline));
            buffer.erase(0, newline + 1);
        }
    }
    ::close(fd);
    std::lock_guard<std::mutex> lock(queue_mutex_);
    reader_finished_ = true;
}

void Service::poll_timer_cb(lv_timer_t* timer) {
    auto* self = static_cast<Service*>(lv_timer_get_user_data(timer));
    if (self) {
        self->poll();
    }
}

void Service::poll() {
    std::vector<std::string> lines;
    bool finished = false;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        lines.swap(queue_);
        finished = reader_finished_;
    }
    for (const auto& line : lines) {
        apply(line);
    }
    if (finished && !stopping_ && state_.status != ServiceState::Status::Failed) {
        // A `fatal` event already set Failed with its reason; this is a crash.
        state_.status = ServiceState::Status::Failed;
        state_.status_text = "Daemon exited unexpectedly";
        state_.transfer.reset();
        state_.request.reset();
        LOG_ERROR("zerosend-daemon exited: {}", state_.status_text);
        changed();
    }
}

void Service::changed() {
    lv_subject_set_int(&revision_, ++revision_value_);
}

void Service::apply(const std::string& line) {
    const auto event = json::parse(line, nullptr, false);
    if (event.is_discarded() || !event.is_object()) {
        LOG_WARN("daemon: unparsable line: {}", line.substr(0, 200));
        return;
    }
    const auto ev = str(event, "ev");

    if (ev == "starting") {
        state_.status = ServiceState::Status::Starting;
        state_.status_text = "Generating identity";
    }
    else if (ev == "ready") {
        state_.status = ServiceState::Status::Ready;
        state_.status_text = "Ready";
        state_.alias = str(event, "alias");
        state_.fingerprint = str(event, "fingerprint");
        state_.port = static_cast<int>(u64(event, "port"));
        state_.destination = str(event, "dest");
        state_.multicast = flag(event, "multicast", true);
        state_.quick_save = flag(event, "quick_save");
        state_.ips.clear();
        if (event.contains("ips") && event["ips"].is_array()) {
            for (const auto& ip : event["ips"]) {
                if (ip.is_string()) {
                    state_.ips.push_back(ip.get<std::string>());
                }
            }
        }
        LOG_INFO("daemon ready as '{}' on port {}", state_.alias, state_.port);
    }
    else if (ev == "fatal") {
        state_.status = ServiceState::Status::Failed;
        state_.status_text = str(event, "text");
        LOG_ERROR("daemon fatal: {}", state_.status_text);
    }
    else if (ev == "scan") {
        state_.scanning = flag(event, "active");
    }
    else if (ev == "devices") {
        state_.devices.clear();
        if (event.contains("list") && event["list"].is_array()) {
            for (const auto& item : event["list"]) {
                Device device;
                device.fingerprint = str(item, "fp");
                device.alias = str(item, "alias");
                device.model = str(item, "model");
                device.type = str(item, "type");
                device.host = str(item, "host");
                device.port = static_cast<int>(u64(item, "port"));
                device.https = flag(item, "https", true);
                device.paired = flag(item, "paired");
                state_.devices.push_back(std::move(device));
            }
        }
    }
    else if (ev == "message") {
        InboxItem item;
        item.is_message = true;
        item.from = str(event, "alias");
        item.text = str(event, "text");
        item.received_at = static_cast<int64_t>(std::time(nullptr));
        state_.inbox.push_front(item);
        state_.popup = item;
    }
    else if (ev == "request") {
        IncomingRequest request;
        request.alias = str(event, "alias");
        request.model = str(event, "model");
        request.count = static_cast<int>(u64(event, "count"));
        request.total = u64(event, "total");
        if (event.contains("files") && event["files"].is_array()) {
            for (const auto& file : event["files"]) {
                request.files.push_back({str(file, "name"), u64(file, "size")});
            }
        }
        state_.request = std::move(request);
    }
    else if (ev == "request_ended") {
        state_.request.reset();
        state_.last_log = str(event, "alias") + ": " + str(event, "text");
    }
    else if (ev == "recv_start") {
        state_.request.reset();
        Transfer transfer;
        transfer.direction = Transfer::Direction::Receive;
        transfer.phase = Transfer::Phase::Running;
        transfer.alias = str(event, "alias");
        transfer.count = static_cast<int>(u64(event, "count"));
        transfer.total = u64(event, "total");
        state_.transfer = transfer;
    }
    else if (ev == "send_waiting") {
        Transfer transfer;
        transfer.direction = Transfer::Direction::Send;
        transfer.phase = Transfer::Phase::Waiting;
        transfer.alias = str(event, "alias");
        transfer.is_text = flag(event, "text");
        transfer.count = static_cast<int>(u64(event, "count"));
        transfer.total = u64(event, "total");
        state_.transfer = transfer;
    }
    else if (ev == "send_accepted") {
        if (state_.transfer) {
            state_.transfer->phase = Transfer::Phase::Running;
            state_.transfer->total = u64(event, "total");
        }
    }
    else if (ev == "progress") {
        if (state_.transfer && state_.transfer->phase != Transfer::Phase::Done) {
            state_.transfer->phase = Transfer::Phase::Running;
            state_.transfer->done = u64(event, "done");
            state_.transfer->total = u64(event, "total");
            state_.transfer->speed = u64(event, "speed");
        }
    }
    else if (ev == "send_done" || ev == "recv_done") {
        const bool receive = ev == "recv_done";
        Transfer transfer = state_.transfer.value_or(Transfer{});
        transfer.direction = receive ? Transfer::Direction::Receive : Transfer::Direction::Send;
        transfer.phase = Transfer::Phase::Done;
        transfer.ok = flag(event, "ok");
        transfer.result = str(event, "text");
        if (transfer.alias.empty()) {
            transfer.alias = str(event, "alias");
        }
        if (transfer.ok) {
            transfer.done = transfer.total;
        }
        state_.transfer = transfer;

        if (receive && u64(event, "files") > 0) {
            InboxItem item;
            item.from = str(event, "alias");
            item.text = transfer.result;
            item.received_at = static_cast<int64_t>(std::time(nullptr));
            if (event.contains("paths") && event["paths"].is_array()) {
                for (const auto& path : event["paths"]) {
                    if (path.is_string()) {
                        item.paths.push_back(path.get<std::string>());
                    }
                }
            }
            state_.inbox.push_front(std::move(item));
        }
    }
    else if (ev == "log") {
        state_.last_log = str(event, "text");
        LOG_INFO("daemon: {}", state_.last_log);
    }
    else if (ev == "error") {
        state_.last_log = str(event, "text");
        LOG_WARN("daemon error: {}", state_.last_log);
    }
    else if (ev == "stopped") {
        // Only ever seen during stop().
    }
    else {
        return;
    }

    while (state_.inbox.size() > kMaxInboxItems) {
        state_.inbox.pop_back();
    }
    changed();
}

void Service::write_command(const std::string& line) {
    if (stdin_fd_ < 0) {
        return;
    }
    if (!write_all(stdin_fd_, line + "\n")) {
        LOG_WARN("daemon stdin closed: {}", std::strerror(errno));
    }
}

void Service::refresh() {
    write_command(R"({"cmd":"refresh"})");
}

void Service::send_text(const std::string& fingerprint, const std::string& text) {
    write_command(json{{"cmd", "send_text"}, {"fp", fingerprint}, {"text", text}}.dump(-1, ' ', false, json::error_handler_t::replace));
}

void Service::send_files(const std::string& fingerprint, const std::vector<std::string>& paths) {
    write_command(json{{"cmd", "send_files"}, {"fp", fingerprint}, {"paths", paths}}.dump(-1, ' ', false, json::error_handler_t::replace));
}

void Service::answer(bool accept, bool pair) {
    write_command(json{{"cmd", "answer"}, {"accept", accept}, {"pair", pair}}.dump());
}

void Service::cancel() {
    write_command(R"({"cmd":"cancel"})");
}

void Service::unpair(const std::string& fingerprint) {
    write_command(json{{"cmd", "unpair"}, {"fp", fingerprint}}.dump());
}

void Service::dismiss_popup() {
    if (state_.popup) {
        state_.popup.reset();
        changed();
    }
}

void Service::dismiss_transfer() {
    if (state_.transfer && state_.transfer->phase == Transfer::Phase::Done) {
        state_.transfer.reset();
        changed();
    }
}

void Service::mark_inbox_read() {
    bool any = false;
    for (auto& item : state_.inbox) {
        any = any || item.unread;
        item.unread = false;
    }
    if (any) {
        changed();
    }
}

int Service::unread_count() const {
    int count = 0;
    for (const auto& item : state_.inbox) {
        count += item.unread ? 1 : 0;
    }
    return count;
}

void Service::clear_inbox() {
    state_.inbox.clear();
    changed();
}

} // namespace net
