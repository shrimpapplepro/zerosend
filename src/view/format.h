/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "ui_const.h"

#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace view {

// Decimal units, matching what LocalSend itself shows on the other device.
inline std::string format_bytes(uint64_t bytes) {
    static constexpr const char* kUnits[] = {"B", "KB", "MB", "GB", "TB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1000.0 && unit < 4) {
        value /= 1000.0;
        ++unit;
    }
    char buffer[32];
    if (unit == 0) {
        std::snprintf(buffer, sizeof(buffer), "%llu B", static_cast<unsigned long long>(bytes));
    } else {
        std::snprintf(buffer, sizeof(buffer), value < 10 ? "%.1f %s" : "%.0f %s", value, kUnits[unit]);
    }
    return buffer;
}

inline std::string format_age(int64_t unix_seconds) {
    const auto delta = static_cast<int64_t>(std::time(nullptr)) - unix_seconds;
    char buffer[24];
    if (delta < 60) {
        return "now";
    }
    if (delta < 3600) {
        std::snprintf(buffer, sizeof(buffer), "%lldm", static_cast<long long>(delta / 60));
    } else if (delta < 86400) {
        std::snprintf(buffer, sizeof(buffer), "%lldh", static_cast<long long>(delta / 3600));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%lldd", static_cast<long long>(delta / 86400));
    }
    return buffer;
}

inline const char* device_icon(const std::string& type) {
    if (type == "mobile") return ICON_DEVICE_MOBILE;
    if (type == "desktop") return ICON_DESKTOP;
    if (type == "web") return ICON_GLOBE;
    if (type == "headless") return ICON_TERMINAL;
    if (type == "server") return ICON_SERVER;
    return ICON_LAPTOP;
}

// One line of a possibly multi-line text, for previews.
inline std::string first_line(const std::string& text, std::size_t max_bytes = 120) {
    auto line = text.substr(0, text.find('\n'));
    if (line.size() > max_bytes) {
        // Cut on a UTF-8 boundary so a Vietnamese or emoji character is never halved.
        std::size_t cut = max_bytes;
        while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        line = line.substr(0, cut) + "...";
    }
    return line;
}

} // namespace view
