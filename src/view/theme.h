/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "lvgl.h"

namespace view {

constexpr int kScreenWidth    = 320;
constexpr int kScreenHeight   = 170;
constexpr int kTitleBarHeight = 30;
// The compact key-hint bar from the CP0 key spec: one text row, 20px, so the
// page above it keeps as much of the 170px as possible.
constexpr int kNavBarHeight   = 20;

struct ThemePalette {
    lv_color_t background;
    lv_color_t surface;
    lv_color_t bar;
    lv_color_t button;
    lv_color_t border;
    lv_color_t text;
    // Reserved for genuinely DISABLED controls. Secondary content text uses
    // `text` with a lighter font instead -- at #c2c2c2 on white this is far
    // too faint to read as body copy.
    lv_color_t text_disabled;
    lv_color_t primary;
    lv_color_t info;
    lv_color_t warning;
    lv_color_t error;
};

ThemePalette palette(bool dark_mode);
void apply_lvgl_theme(lv_display_t* display, bool dark_mode);

} // namespace view
