/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tab_screen.h"

#include "asset_manager.h"
#include "linux_input.h"

namespace screen {

TabScreen::TabScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {}

TabScreen::~TabScreen() {
    platform::clear_screen_key_handler(this);
}

void TabScreen::install_key_handler() {
    platform::set_screen_key_handler(key_handler, this);
}

bool TabScreen::on_key(uint32_t key) {
    LV_UNUSED(key);
    return false;
}

const lv_font_t* TabScreen::font(const char* file, uint32_t size, const lv_font_t* fallback) {
    const lv_font_t* loaded = assets().load_font(file, size);
    return loaded ? loaded : fallback;
}

bool TabScreen::key_handler(uint32_t key, void* user_data) {
    auto* self = static_cast<TabScreen*>(user_data);
    if (!self) {
        return false;
    }
    if (self->on_key(key)) {
        return true;
    }
    switch (key) {
        case LV_KEY_LEFT:
            self->view_model().previous_tab();
            return true;
        case LV_KEY_RIGHT:
            self->view_model().next_tab();
            return true;
        case '1':
        case '2':
        case '3':
            self->view_model().open_page(static_cast<model::AppPage>(key - '1'));
            return true;
        default:
            return false;
    }
}

} // namespace screen
