/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <sys/types.h>
#include <thread>
#include <vector>

namespace net {

// The LocalSend side of the app. All protocol work happens in
// zerosend-daemon, a child process built on LocalSend's own Rust core; this
// class starts it, speaks its JSON-lines wire, and keeps the state the screens
// render. Everything here runs on the LVGL thread except the stdout reader,
// which only fills a queue that poll() drains.
//
// Screens observe revision_subject() and re-read state() when it changes.

struct Device {
    std::string fingerprint;
    std::string alias;
    std::string model;
    std::string type; // mobile | desktop | web | headless | server | unknown
    std::string host;
    int port{0};
    bool https{true};
    bool paired{false};
};

struct OfferedFile {
    std::string name;
    uint64_t size{0};
};

// An incoming transfer waiting for the user's yes or no.
struct IncomingRequest {
    std::string alias;
    std::string model;
    int count{0};
    uint64_t total{0};
    std::vector<OfferedFile> files;
};

struct Transfer {
    enum class Direction { Send, Receive };
    enum class Phase {
        Waiting, // sent the offer, the receiver has not answered
        Running,
        Done,
    };
    Direction direction{Direction::Send};
    Phase phase{Phase::Waiting};
    std::string alias;
    bool is_text{false}; // a text: it travels inside the request, no upload
    int count{0};
    uint64_t done{0};
    uint64_t total{0};
    uint64_t speed{0};
    // No bytes moved for kStallAfter while Running: the other device most
    // likely left the network. Shown instead of a frozen progress bar.
    bool stalled{false};
    int64_t last_change_ms{0}; // steady clock, when `done` last moved
    bool ok{false};
    std::string result; // human summary once Done
};

struct InboxItem {
    bool is_message{false};
    std::string from;
    std::string text;                // the message, or a summary of the files
    std::vector<std::string> paths;  // saved files
    int64_t received_at{0};          // unix seconds
    bool unread{true};
};

struct ServiceState {
    enum class Status { Starting, Ready, Failed };
    Status status{Status::Starting};
    std::string status_text{"Starting"};
    std::string alias;
    std::string fingerprint;
    std::vector<std::string> ips;
    int port{0};
    std::string destination;
    bool multicast{true};
    bool quick_save{false};
    bool scanning{false};
    std::vector<Device> devices;
    std::optional<IncomingRequest> request;
    std::optional<Transfer> transfer;
    std::deque<InboxItem> inbox; // newest first, bounded
    // The newest message, shown as a popup until dismissed.
    std::optional<InboxItem> popup;
    std::string last_log;
};

struct ServiceOptions {
    std::string daemon_path;
    std::string alias;       // empty: the daemon uses the hostname
    int port{53317};
    std::string destination; // empty: the daemon uses ~/Downloads
    std::string log_path;    // the daemon's stderr; empty discards it
    bool quick_save{false};  // accept every request without asking
};

// The user-editable part of ServiceOptions, persisted as key=value lines.
bool load_service_settings(const std::string& path, ServiceOptions& options);
bool save_service_settings(const std::string& path, const ServiceOptions& options);

class Service {
public:
    static Service& instance();

    void start(const ServiceOptions& options);
    void stop();

    // Settings: where they persist, the current values, and a change. A new
    // name, port or folder restarts the daemon (the HTTPS server and the
    // announcement both carry them); Quick Save is applied live.
    void set_settings_path(std::string path) { settings_path_ = std::move(path); }
    const ServiceOptions& options() const { return options_; }
    void update_options(const ServiceOptions& options);

    const ServiceState& state() const { return state_; }
    lv_subject_t* revision_subject() { return &revision_; }

    void refresh();
    void send_text(const std::string& fingerprint, const std::string& text);
    void send_files(const std::string& fingerprint, const std::vector<std::string>& paths);
    void answer(bool accept, bool pair);
    void cancel();
    void unpair(const std::string& fingerprint);

    // UI-side bookkeeping, no daemon involved.
    void dismiss_popup();
    void dismiss_transfer();
    void mark_inbox_read();
    int unread_count() const;
    void clear_inbox();

private:
    Service();
    ~Service();

    void write_command(const std::string& line);
    void reader_loop(int fd);
    void poll();
    void apply(const std::string& line);
    void changed();
    static void poll_timer_cb(lv_timer_t* timer);

    ServiceOptions options_;
    std::string settings_path_;
    ServiceState state_;
    lv_subject_t revision_{};
    int32_t revision_value_{0};
    lv_timer_t* poll_timer_{nullptr};

    pid_t pid_{-1};
    int stdin_fd_{-1};
    std::thread reader_;
    std::mutex queue_mutex_;
    std::vector<std::string> queue_;
    bool reader_finished_{false};
    bool stopping_{false};
};

} // namespace net
