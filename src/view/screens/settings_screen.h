/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tab_screen.h"

#include <string>

namespace screen {

// The Settings tab: the LocalSend settings that make sense on this device.
// Up/Down picks a row, Enter toggles it or starts editing; while editing,
// Enter saves and ESC cancels.
class SettingsScreen : public TabScreen {
public:
    SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~SettingsScreen() override;

private:
    enum class Row {
        Alias,
        QuickSave,
        Destination,
        Port,
        DarkMode,
        Encryption,
        Fingerprint,
        About,
        Count,
    };

    void build_content(lv_obj_t* content) override;
    bool on_key(uint32_t key) override;

    void render();
    void activate();
    void commit_edit();
    void stop_editing();
    std::string value_of(Row row);
    static bool editable(Row row);
    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* list_{nullptr};
    int cursor_{0};
    bool editing_{false};
    std::string input_;

    const lv_font_t* font_body_{nullptr};
    const lv_font_t* font_small_{nullptr};
};

} // namespace screen
