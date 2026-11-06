/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "tool_screen.h"

#include "linux_input.h"

namespace screen {

ToolScreen::ToolScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseScreen(view_model, assets) {}

ToolScreen::~ToolScreen() {
    platform::clear_screen_key_handler(this);
}

void ToolScreen::install_key_handler() {
    platform::set_screen_key_handler(key_handler, this);
}

bool ToolScreen::on_key(uint32_t key) {
    LV_UNUSED(key);
    return false;
}

bool ToolScreen::on_escape() {
    return false;
}

bool ToolScreen::key_handler(uint32_t key, void* user_data) {
    auto* self = static_cast<ToolScreen*>(user_data);
    if (!self) {
        return false;
    }

    if (key == LV_KEY_ESC) {
        if (!self->on_escape()) {
            self->view_model().go_home();
        }
        return true;
    }

    return self->on_key(key);
}

} // namespace screen
