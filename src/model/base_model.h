/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace model {

// The three tabs every LocalSend app has, then the pages opened from Send.
enum class AppPage {
    Receive  = 0,
    Send     = 1,
    Settings = 2,
    Compose  = 3, // type a text
    Files    = 4, // pick files
};

constexpr int kTabCount = 3;

inline bool is_tab(AppPage page) {
    return static_cast<int>(page) < kTabCount;
}

// Name for the title bar on a sub-page. Tabs show the tab strip instead.
const char* page_title(AppPage page);
const char* tab_title(int tab);

// What the Send tab sends: files, or one text. Exclusive, like a fresh
// selection in LocalSend: picking files replaces a text and the other way round.
struct Selection {
    std::vector<std::string> paths;
    uint64_t total_bytes{0};
    std::string text;
    bool empty() const { return paths.empty() && text.empty(); }
};

class BaseModel {
public:
    const char* app_title() const;

    bool dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    AppPage current_page() const;
    void set_current_page(AppPage page);

    // A direct target for the Compose page: set when Enter is pressed on a
    // device with nothing selected, so a quick text needs no detour. Empty
    // means Compose fills the selection instead.
    const std::string& target_fingerprint() const;
    const std::string& target_alias() const;
    void set_target(std::string fingerprint, std::string alias);

    Selection& selection() { return selection_; }
    const Selection& selection() const { return selection_; }

    // Send tab cursor, kept across page visits.
    int selected_device() const;
    void set_selected_device(int index);

private:
    bool dark_mode_ = true;
    AppPage current_page_ = AppPage::Receive;
    Selection selection_;
    std::string target_fingerprint_;
    std::string target_alias_;
    int selected_device_ = 0;
};

} // namespace model
