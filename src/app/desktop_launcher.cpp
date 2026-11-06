/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "desktop_launcher.h"

#if USE_DESKTOP

#include "linux_input.h"
#include "logger.h"
#include "screen_manager.h"
#include "theme.h"

#include <ctime>
#include <string>

namespace app {
namespace {

// Geometry measured off the web emulator's 320x170 framebuffer, so the mock
// lands where the real launcher puts things.
constexpr int kStatusY        = 9;
constexpr int kTileBaseline   = 122;   // all three tiles sit on this line
constexpr int kTileBig        = 100;
constexpr int kTileSmall      = 80;
constexpr int kTilePitch      = 99;    // centre-to-centre
constexpr int kLabelY         = 126;
constexpr int kDotY           = 155;
constexpr int kDotSize        = 6;
constexpr int kDotPitch       = 10;

constexpr uint32_t kDotActive = 0xCFD430;
constexpr uint32_t kDotIdle   = 0x303030;
constexpr uint32_t kTileEdge  = 0x444444;
constexpr uint32_t kSignal    = 0x3ADB4A;

struct Entry {
    const char* label;
    const char* icon_100;
    const char* icon_80;
    bool is_this_app;
};

// "Keep it as short as possible. It is displayed in the CardputerZero launcher
// and store" -- CP0 app spec. Hence SEND, not the full package name.
const std::array<Entry, 3>& entries() {
    static const std::array<Entry, 3> kEntries = {{
        {"STORE", "applaunch/images/store_100.png", "applaunch/images/store_80.png", false},
        {"SEND", "images/zerosend_100.png", "images/zerosend_80.png", true},
        {"SETTING", "applaunch/images/setting_100.png", "applaunch/images/setting_80.png", false},
    }};
    return kEntries;
}

std::string lvgl_path(const AssetManager& assets, const char* relative) {
    const auto resolved = assets.resolve(relative);
    if (resolved.empty()) {
        return {};
    }
    return "A:" + resolved.string();
}

std::string clock_text() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[8] = {};
    std::strftime(buffer, sizeof(buffer), "%H:%M", &local);
    return buffer;
}

lv_obj_t* image_at(lv_obj_t* parent, const std::string& source, int x, int y) {
    if (source.empty()) {
        return nullptr;
    }

    auto* image = lv_image_create(parent);
    lv_image_set_src(image, source.c_str());
    lv_obj_align(image, LV_ALIGN_TOP_LEFT, x, y);
    return image;
}

} // namespace

DesktopLauncher::DesktopLauncher(AssetManager& assets,
                                 viewmodel::BaseViewModel& view_model,
                                 ScreenManager& screen_manager)
    : assets_(assets), view_model_(view_model), screen_manager_(screen_manager) {}

DesktopLauncher::~DesktopLauncher() {
    platform::clear_screen_key_handler(this);

    if (screen_ && lv_obj_is_valid(screen_)) {
        lv_obj_delete(screen_);
    }
}

void DesktopLauncher::show() {
    build();
    if (!screen_) {
        return;
    }

    if (clock_label_) {
        lv_label_set_text(clock_label_, clock_text().c_str());
    }

    showing_ = true;
    apply_selection();
    lv_screen_load(screen_);
    platform::set_screen_key_handler(key_handler, this);
}

bool DesktopLauncher::showing() const {
    return showing_;
}

void DesktopLauncher::build() {
    if (screen_) {
        return;
    }

    screen_ = lv_obj_create(nullptr);
    lv_obj_remove_style_all(screen_);
    lv_obj_set_size(screen_, view::kScreenWidth, view::kScreenHeight);
    lv_obj_set_style_bg_color(screen_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(screen_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    // The launcher is system UI: it uses M5's own font, not the app's.
    const lv_font_t* font = assets_.load_font("Montserrat-Bold.ttf", 15);
    const lv_font_t* small = assets_.load_font("Montserrat-Bold.ttf", 11);
    if (!font) {
        font = &lv_font_montserrat_14;
    }
    if (!small) {
        small = &lv_font_montserrat_12;
    }

    build_status_bar(small);

    image_at(screen_, lvgl_path(assets_, "applaunch/images/zuo_logo.png"), 4, 66);
    image_at(screen_, lvgl_path(assets_, "applaunch/images/you_logo.png"), 305, 66);

    for (std::size_t index = 0; index < kEntryCount; ++index) {
        const auto& entry = entries()[index];

        auto* tile = lv_obj_create(screen_);
        lv_obj_remove_style_all(tile);
        lv_obj_set_style_radius(tile, 18, 0);
        lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(tile, 1, 0);
        lv_obj_set_style_border_color(tile, lv_color_hex(kTileEdge), 0);
        // Full-bleed icons are square by spec -- the launcher is what rounds
        // them off, so the tile has to clip its own corners.
        lv_obj_set_style_clip_corner(tile, true, 0);
        lv_obj_remove_flag(tile, LV_OBJ_FLAG_SCROLLABLE);
        tiles_[index] = tile;

        auto* icon = lv_image_create(tile);
        lv_obj_center(icon);
        icons_[index] = icon;

        auto* label = lv_label_create(screen_);
        lv_label_set_text(label, entry.label);
        lv_obj_set_style_text_font(label, font, 0);
        lv_obj_set_style_text_color(label, lv_color_white(), 0);
        labels_[index] = label;
    }

    for (std::size_t index = 0; index < kEntryCount; ++index) {
        auto* dot = lv_obj_create(screen_);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, kDotSize, kDotSize);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_align(dot, LV_ALIGN_TOP_MID,
                     (static_cast<int>(index) - 1) * kDotPitch, kDotY);
        dots_[index] = dot;
    }
}

void DesktopLauncher::build_status_bar(const lv_font_t* font) {
    image_at(screen_, lvgl_path(assets_, "applaunch/images/zero.png"), 8, 11);

    // Signal strength: five rising bars, drawn rather than extracted -- the
    // launcher builds these from its own widgets, there is no image for them.
    for (int bar = 0; bar < 5; ++bar) {
        auto* rect = lv_obj_create(screen_);
        lv_obj_remove_style_all(rect);
        const int height = 4 + bar * 2;
        lv_obj_set_size(rect, 3, height);
        lv_obj_set_style_bg_opa(rect, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(rect, lv_color_hex(kSignal), 0);
        lv_obj_align(rect, LV_ALIGN_TOP_LEFT, 209 + bar * 4, 22 - height);
    }

    image_at(screen_, lvgl_path(assets_, "applaunch/images/time_bg.png"), 235, kStatusY);
    clock_label_ = lv_label_create(screen_);
    lv_label_set_text(clock_label_, "--:--");
    lv_obj_set_style_text_font(clock_label_, font, 0);
    lv_obj_set_style_text_color(clock_label_, lv_color_white(), 0);
    lv_obj_align(clock_label_, LV_ALIGN_TOP_LEFT, 240, kStatusY + 2);

    image_at(screen_, lvgl_path(assets_, "applaunch/images/battery_bg.png"), 277, kStatusY + 1);
    auto* battery = lv_label_create(screen_);
    lv_label_set_text(battery, "85%");
    lv_obj_set_style_text_font(battery, font, 0);
    lv_obj_set_style_text_color(battery, lv_color_white(), 0);
    lv_obj_align(battery, LV_ALIGN_TOP_LEFT, 283, kStatusY + 3);
}

void DesktopLauncher::apply_selection() {
    for (std::size_t index = 0; index < kEntryCount; ++index) {
        const int offset = static_cast<int>(index) - static_cast<int>(selected_);
        const bool centered = offset == 0;
        const int size = centered ? kTileBig : kTileSmall;
        const int centre_x = 160 + offset * kTilePitch;

        if (auto* tile = tiles_[index]) {
            lv_obj_set_size(tile, size, size);
            lv_obj_set_pos(tile, centre_x - size / 2, kTileBaseline - size);
            lv_obj_set_style_border_opa(tile, centered ? LV_OPA_COVER : LV_OPA_40, 0);
        }

        if (auto* icon = icons_[index]) {
            const auto& entry = entries()[index];
            const auto source = lvgl_path(assets_, centered ? entry.icon_100 : entry.icon_80);
            if (!source.empty()) {
                lv_image_set_src(icon, source.c_str());
                lv_obj_center(icon);
            }
        }

        if (auto* label = labels_[index]) {
            lv_obj_align(label, LV_ALIGN_TOP_MID, centre_x - 160, kLabelY);
            lv_obj_set_style_text_opa(label, centered ? LV_OPA_COVER : LV_OPA_70, 0);
        }

        if (auto* dot = dots_[index]) {
            lv_obj_set_style_bg_color(dot,
                                      lv_color_hex(index == selected_ ? kDotActive : kDotIdle),
                                      0);
        }
    }
}

void DesktopLauncher::move(int delta) {
    const int count = static_cast<int>(kEntryCount);
    const int next = (static_cast<int>(selected_) + delta + count) % count;
    selected_ = static_cast<std::size_t>(next);
    apply_selection();
}

void DesktopLauncher::launch() {
    if (!entries()[selected_].is_this_app) {
        // The neighbours are scenery -- their icons are M5's, their apps are
        // not here. Say so rather than doing nothing.
        if (auto* label = labels_[selected_]) {
            lv_label_set_text(label, "NOT HERE");
        }
        return;
    }

    showing_ = false;
    platform::clear_screen_key_handler(this);
    view_model_.open_page(model::AppPage::Receive);
    // The app's screens are not built until something is launched, which is
    // also what keeps ScreenManager from loading a page over the launcher.
    screen_manager_.start();
    // Rebuild rather than reveal: opening an app from the launcher starts it
    // fresh on the device, and a rebuilt screen re-registers its key handler.
    screen_manager_.reload_current_page();
    LOG_INFO("launcher: opened {}", entries()[selected_].label);
}

bool DesktopLauncher::key_handler(uint32_t key, void* user_data) {
    auto* self = static_cast<DesktopLauncher*>(user_data);
    if (!self || !self->showing_) {
        return false;
    }

    switch (key) {
        case LV_KEY_LEFT:
            self->move(-1);
            return true;
        case LV_KEY_RIGHT:
            self->move(1);
            return true;
        case LV_KEY_ENTER:
            self->launch();
            return true;
        default:
            break;
    }

    // Nothing else reaches the app while the launcher owns the screen.
    return true;
}

} // namespace app

#endif
