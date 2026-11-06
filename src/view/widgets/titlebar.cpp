/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "titlebar.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>

#include "asset_manager.h"
#include "bindings.h"
#include "device_status.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

constexpr uint32_t kDeviceRefreshTicks = 5;

const char* wifi_icon(int32_t strength_percent, bool connected) {
    if (!connected || strength_percent < 0) {
        return ICON_WIFI_NONE;
    }
    if (strength_percent < 35) {
        return ICON_WIFI_LOW;
    }
    if (strength_percent < 70) {
        return ICON_WIFI_MEDIUM;
    }
    return ICON_WIFI_HIGH;
}

const char* battery_icon(int32_t capacity_percent, bool charging) {
    if (charging) {
        return ICON_BAT_CHARGING;
    }
    if (capacity_percent >= 95) {
        return ICON_BAT_FULL;
    }
    if (capacity_percent >= 70) {
        return ICON_BAT_HIGH;
    }
    if (capacity_percent >= 35) {
        return ICON_BAT_MEDIUM;
    }
    if (capacity_percent >= 10) {
        return ICON_BAT_LOW;
    }
    return ICON_BAT_EMPTY;
}

void configure_label(lv_obj_t* label,
                     lv_subject_t* dark_mode_subject,
                     const lv_font_t* font,
                     int32_t width) {
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    reactive::bind_theme(label, dark_mode_subject, reactive::ThemeRole::Text);
}

void set_visible(lv_obj_t* obj, bool visible) {
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
    else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

} // namespace

TitleBar::TitleBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

TitleBar::~TitleBar() {
    if (refresh_timer_) {
        lv_timer_delete(refresh_timer_);
        refresh_timer_ = nullptr;
    }
}

void TitleBar::page_changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);

    auto* title_bar = static_cast<TitleBar*>(lv_observer_get_user_data(observer));
    if (title_bar) {
        title_bar->refresh_title();
    }
}

void TitleBar::refresh_title() {
    if (!app_title_ || !page_title_ || !separator_ || !tab_row_) {
        return;
    }

    const auto colors = view::palette(view_model_.is_dark_mode());
    const auto current = view_model_.current_page();

    // Tabs: the strip LocalSend shows on every platform, active one lit and
    // underlined. Sub-pages: "ZeroSend / Text", the app name stepping back.
    if (model::is_tab(current)) {
        lv_obj_add_flag(title_row_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(tab_row_, LV_OBJ_FLAG_HIDDEN);
        for (int tab = 0; tab < model::kTabCount; ++tab) {
            auto* label = tab_labels_[tab];
            const bool active = tab == static_cast<int>(current);
            lv_obj_set_style_text_color(label, active ? colors.primary : colors.text, 0);
            lv_obj_set_style_text_opa(label, active ? LV_OPA_COVER : LV_OPA_60, 0);
            lv_obj_set_style_border_side(label, LV_BORDER_SIDE_BOTTOM, 0);
            lv_obj_set_style_border_color(label, colors.primary, 0);
            lv_obj_set_style_border_width(label, active ? 2 : 0, 0);
        }
        return;
    }

    lv_obj_add_flag(tab_row_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(title_row_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_font(app_title_, font_app_small_, 0);
    lv_obj_set_style_text_color(app_title_, colors.text_disabled, 0);
    lv_label_set_text(page_title_, model::page_title(current));
    lv_obj_remove_flag(page_title_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(separator_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_text_color(separator_, colors.text_disabled, 0);
}

void TitleBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kTitleBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_clear_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    font_app_large_ = assets_.load_font("inter-semibold.ttf", 14);
    font_app_small_ = assets_.load_font("inter-regular.ttf", 10);
    font_page_ = assets_.load_font("inter-semibold.ttf", 13);
    if (!font_app_large_) font_app_large_ = &lv_font_montserrat_14;
    if (!font_app_small_) font_app_small_ = &lv_font_montserrat_12;
    if (!font_page_) font_page_ = &lv_font_montserrat_14;

    // On a tool page the app name shrinks and steps back so the page name can
    // lead. On Home there is no page name, so the app name takes full size.
    auto* title_row = lv_obj_create(core_obj_);
    title_row_ = title_row;
    lv_obj_remove_style_all(title_row);
    lv_obj_set_size(title_row, 160, LV_PCT(100));
    lv_obj_align(title_row, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_flex_flow(title_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(title_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(title_row, 4, 0);
    lv_obj_remove_flag(title_row, LV_OBJ_FLAG_SCROLLABLE);

    tab_row_ = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(tab_row_);
    lv_obj_set_size(tab_row_, 170, LV_PCT(100));
    lv_obj_align(tab_row_, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_set_flex_flow(tab_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_row_, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(tab_row_, 10, 0);
    lv_obj_remove_flag(tab_row_, LV_OBJ_FLAG_SCROLLABLE);
    for (int tab = 0; tab < model::kTabCount; ++tab) {
        auto* label = lv_label_create(tab_row_);
        lv_label_set_text(label, model::tab_title(tab));
        lv_obj_set_style_text_font(label, font_page_, 0);
        lv_obj_set_style_pad_bottom(label, 2, 0);
        tab_labels_[tab] = label;
    }

    app_title_ = lv_label_create(title_row);
    lv_label_bind_text(app_title_, view_model_.title_subject(), nullptr);
    lv_label_set_long_mode(app_title_, LV_LABEL_LONG_DOT);
    reactive::bind_theme(app_title_, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);

    separator_ = lv_label_create(title_row);
    lv_label_set_text(separator_, "/");
    lv_obj_set_style_text_font(separator_, font_app_small_, 0);

    page_title_ = lv_label_create(title_row);
    lv_label_set_long_mode(page_title_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(page_title_, font_page_, 0);
    reactive::bind_theme(page_title_, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);

    reactive::observe_obj(core_obj_, view_model_.current_page_subject(), page_changed_cb, this);
    reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), page_changed_cb, this);
    refresh_title();

    auto* status = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(status);
    lv_obj_set_size(status, 145, LV_PCT(100));
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -5, 0);
    lv_obj_set_flex_flow(status, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(status, 3, 0);
    lv_obj_clear_flag(status, LV_OBJ_FLAG_SCROLLABLE);

    const lv_font_t* icon_font = assets_.load_font("Phosphor-Fill.ttf", 16);
    if (!icon_font) {
        icon_font = &lv_font_montserrat_14;
    }
    wifi_label_ = lv_label_create(status);
    configure_label(wifi_label_, view_model_.dark_mode_subject(), icon_font, 18);
    ethernet_label_ = lv_label_create(status);
    configure_label(ethernet_label_, view_model_.dark_mode_subject(), icon_font, 18);
    battery_icon_label_ = lv_label_create(status);
    configure_label(battery_icon_label_, view_model_.dark_mode_subject(), icon_font, 18);
    battery_percent_label_ = lv_label_create(status);
    configure_label(battery_percent_label_, view_model_.dark_mode_subject(), &lv_font_montserrat_14, 40);
    time_label_ = lv_label_create(status);
    configure_label(time_label_, view_model_.dark_mode_subject(), &lv_font_montserrat_14, 42);

    refresh_time();
    refresh_device_status();
    refresh_timer_ = lv_timer_create(refresh_timer_cb, 1000, this);
}

void TitleBar::refresh_time() {
    const std::time_t now = std::time(nullptr);
    std::tm local_time{};
#if defined(_WIN32)
    localtime_s(&local_time, &now);
#else
    localtime_r(&now, &local_time);
#endif
    std::array<char, 6> text{};
    std::strftime(text.data(), text.size(), "%H:%M", &local_time);
    lv_label_set_text(time_label_, text.data());
}

void TitleBar::refresh_device_status() {
    const auto status = platform::read_device_status();

    lv_label_set_text(wifi_label_, wifi_icon(status.wifi_strength_percent, status.wifi_connected));
    set_visible(wifi_label_, status.wifi_connected);

    lv_label_set_text(ethernet_label_, ICON_ETHERNET);
    set_visible(ethernet_label_, status.ethernet_connected);

    lv_label_set_text(battery_icon_label_,
                      battery_icon(status.battery_percent, status.battery_charging));
    set_visible(battery_icon_label_, status.battery_present);
    set_visible(battery_percent_label_, status.battery_present);
    if (status.battery_present) {
        std::array<char, 6> percent{};
        std::snprintf(percent.data(), percent.size(), "%d%%", std::clamp(status.battery_percent, 0, 100));
        lv_label_set_text(battery_percent_label_, percent.data());
    }
}

void TitleBar::refresh_timer_cb(lv_timer_t* timer) {
    auto* title_bar = static_cast<TitleBar*>(lv_timer_get_user_data(timer));
    if (!title_bar) {
        return;
    }
    title_bar->refresh_time();
    if (++title_bar->refresh_tick_ >= kDeviceRefreshTicks) {
        title_bar->refresh_tick_ = 0;
        title_bar->refresh_device_status();
    }
}

} // namespace view::widgets
