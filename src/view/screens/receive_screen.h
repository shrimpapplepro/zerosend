/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tab_screen.h"

namespace screen {

// The Receive tab: who this device is to the others (its name and address),
// whether it is ready, and the history of what arrived. Up/Down walks the
// history, Enter opens an entry, DEL clears it.
class ReceiveScreen : public TabScreen {
public:
    ReceiveScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
    bool on_key(uint32_t key) override;

    void render();
    void render_detail();
    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* content_{nullptr};
    lv_obj_t* alias_{nullptr};
    lv_obj_t* address_{nullptr};
    lv_obj_t* status_{nullptr};
    lv_obj_t* list_{nullptr};
    lv_obj_t* detail_{nullptr};
    int cursor_{0};
    bool detail_open_{false};

    const lv_font_t* font_big_{nullptr};
    const lv_font_t* font_body_{nullptr};
    const lv_font_t* font_small_{nullptr};
    const lv_font_t* font_icon_{nullptr};
};

} // namespace screen
