/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#if USE_DESKTOP

#include "asset_manager.h"
#include "base_viewmodel.h"
#include "lvgl.h"

#include <array>

namespace app {

class ScreenManager;

// A stand-in for APPLaunch, the CardputerZero's own launcher, so the simulator
// shows this app the way the device will: an icon in a carousel you open with
// Enter and leave by holding ESC.
//
// The layout is measured off the web emulator and drawn with its extracted
// chrome (see assets/applaunch/images/README). Desktop only -- on hardware the
// real launcher does this, and none of it is packaged.
class DesktopLauncher {
public:
    DesktopLauncher(AssetManager& assets,
                    viewmodel::BaseViewModel& view_model,
                    ScreenManager& screen_manager);
    ~DesktopLauncher();

    DesktopLauncher(const DesktopLauncher&) = delete;
    DesktopLauncher& operator=(const DesktopLauncher&) = delete;

    // Load the launcher and give it the keys. Safe to call while it is already
    // showing.
    void show();
    bool showing() const;

private:
    static constexpr std::size_t kEntryCount = 3;
    static constexpr std::size_t kSelfIndex = 1;

    void build();
    void build_status_bar(const lv_font_t* font);
    void apply_selection();
    void move(int delta);
    void launch();

    static bool key_handler(uint32_t key, void* user_data);

    AssetManager& assets_;
    viewmodel::BaseViewModel& view_model_;
    ScreenManager& screen_manager_;

    lv_obj_t* screen_{nullptr};
    lv_obj_t* clock_label_{nullptr};
    std::array<lv_obj_t*, kEntryCount> tiles_{};
    std::array<lv_obj_t*, kEntryCount> icons_{};
    std::array<lv_obj_t*, kEntryCount> labels_{};
    std::array<lv_obj_t*, kEntryCount> dots_{};
    std::size_t selected_{kSelfIndex};
    bool showing_{false};
};

} // namespace app

#endif
