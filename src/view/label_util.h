/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "lvgl.h"

#include <cstring>

namespace view {

// LVGL redraws a label on every lv_label_set_text() and an object on every
// hidden-flag change, even when nothing changes. Periodic updates (the clock,
// status lines) go through these so an idle screen does not repaint -- on the
// CP0 each repaint is an SPI transfer and a wakeup.
inline bool set_label_text_if_changed(lv_obj_t* label, const char* text) {
    const char* current = lv_label_get_text(label);
    if (current && std::strcmp(current, text) == 0) {
        return false;
    }
    lv_label_set_text(label, text);
    return true;
}

inline void set_visible_if_changed(lv_obj_t* obj, bool visible) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) != visible) {
        return;
    }
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace view
