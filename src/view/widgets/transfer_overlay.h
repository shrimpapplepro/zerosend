/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_viewmodel.h"
#include "lvgl.h"

namespace app {
class AssetManager;
}

namespace view::widgets {

// The card that floats over every page, on LVGL's top layer, for whatever
// needs the user right now -- in priority order:
//   1. an incoming request:   OK accept, P accept and always trust, ESC decline
//   2. a transfer:            progress; ESC cancels
//   3. its result:            OK closes; a success closes itself
//   4. a received text:       OK closes
// While the card is up it takes the keys (platform modal hook), so the page
// underneath never sees an Enter meant for "Accept".
class TransferOverlay {
public:
    TransferOverlay(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~TransferOverlay();

    TransferOverlay(const TransferOverlay&) = delete;
    TransferOverlay& operator=(const TransferOverlay&) = delete;

private:
    enum class Mode { Hidden, Request, Progress, Result, Message };

    void render();
    bool on_key(uint32_t key);
    static bool key_handler(uint32_t key, void* user_data);
    static void changed_cb(lv_observer_t* observer, lv_subject_t* subject);
    static void auto_close_cb(lv_timer_t* timer);

    viewmodel::BaseViewModel& view_model_;
    Mode mode_{Mode::Hidden};
    lv_obj_t* root_{nullptr};
    lv_obj_t* card_{nullptr};
    lv_obj_t* icon_{nullptr};
    lv_obj_t* title_{nullptr};
    lv_obj_t* body_{nullptr};
    lv_obj_t* bar_{nullptr};
    lv_obj_t* hint_{nullptr};
    lv_timer_t* auto_close_{nullptr};
    lv_observer_t* service_observer_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
};

} // namespace view::widgets
