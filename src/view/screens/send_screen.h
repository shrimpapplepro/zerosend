/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tab_screen.h"

namespace screen {

// The Send tab, as in every LocalSend app: a selection on top, nearby devices
// below, and picking a device sends the selection to it.
//
//   P  pick files      T  type a text      R  scan again     DEL  clear selection
//   Enter on a device  send the selection; with nothing selected, open a text
//                      straight to that device
class SendScreen : public TabScreen {
public:
    SendScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    void build_content(lv_obj_t* content) override;
    bool on_key(uint32_t key) override;

    void render();
    void send_to_selected();
    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    lv_obj_t* selection_{nullptr};
    lv_obj_t* selection_icon_{nullptr};
    lv_obj_t* selection_text_{nullptr};
    lv_obj_t* heading_{nullptr};
    lv_obj_t* scanning_{nullptr};
    lv_obj_t* list_{nullptr};

    const lv_font_t* font_title_{nullptr};
    const lv_font_t* font_body_{nullptr};
    const lv_font_t* font_small_{nullptr};
    const lv_font_t* font_icon_{nullptr};
};

} // namespace screen
