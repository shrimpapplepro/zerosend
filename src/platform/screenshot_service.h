/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace platform::screenshot {

struct CaptureResult {
    bool success{false};
    std::string path;
    std::string error;
};

// Snapshots the active LVGL screen -- not the SDL window -- so the PNG is
// exactly 320x170 with no simulator chrome to crop. That makes the desktop
// build the cheapest source of AppStore screenshots.
//
// Ported from CardputerZero/Template (2026-08-21).
CaptureResult capture_active_screen();

} // namespace platform::screenshot
