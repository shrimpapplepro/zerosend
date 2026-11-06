/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "navbar.h"

#include "asset_manager.h"
#include "bindings.h"
#include "theme.h"
#include "ui_const.h"

namespace view::widgets {
namespace {

struct ShortcutSpec {
    const char* key;
    const char* action;
};

// Nothing for the launcher: it builds no bar at all. The bar is for what a key
// does that the screen cannot show on its own.
constexpr std::array<ShortcutSpec, 4> kNoHints = {{
    {"", ""},
    {"", ""},
    {"", ""},
    {"", ""},
}};

constexpr std::array<ShortcutSpec, 4> kReceiveHints = {{
    {"OK", "View"},
    {"DEL", "Clear"},
    {"1-3", "Tabs"},
    {"", ""},
}};

constexpr std::array<ShortcutSpec, 4> kSendHints = {{
    {"OK", "Send"},
    {"P", "Files"},
    {"T", "Text"},
    {"R", "Scan"},
}};

constexpr std::array<ShortcutSpec, 4> kSettingsHints = {{
    {"OK", "Change"},
    {"1-3", "Tabs"},
    {"", ""},
    {"", ""},
}};

// ESC is handled by ToolScreen itself on every sub-page.
constexpr std::array<ShortcutSpec, 4> kComposeHints = {{
    {"ESC", "Back"},
    {"OK", "Done"},
    {"", ""},
    {"", ""},
}};

constexpr std::array<ShortcutSpec, 4> kFilesHints = {{
    {"ESC", "Up"},
    {"OK", "Open"},
    {"SPC", "Mark"},
    {"S", "Select"},
}};

} // namespace

NavBar::NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : BaseWidgets(parent), view_model_(view_model), assets_(assets) {}

NavBar::~NavBar() {
    if (page_observer_) {
        lv_observer_remove(page_observer_);
        page_observer_ = nullptr;
    }
    if (theme_observer_) {
        lv_observer_remove(theme_observer_);
        theme_observer_ = nullptr;
    }
}

void NavBar::build() {
    if (core_obj_) {
        return;
    }

    core_obj_ = lv_obj_create(parent_);
    lv_obj_remove_style_all(core_obj_);
    lv_obj_set_size(core_obj_, LV_PCT(100), kNavBarHeight);
    lv_obj_align(core_obj_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(core_obj_, LV_OBJ_FLAG_SCROLLABLE);
    reactive::bind_theme(core_obj_, view_model_.dark_mode_subject(), reactive::ThemeRole::Bar);

    create_shortcut_hints();
    page_observer_ = reactive::observe_obj(core_obj_, view_model_.current_page_subject(), update_hints_cb, this);
    theme_observer_ = reactive::observe_obj(core_obj_, view_model_.dark_mode_subject(), update_hints_cb, this);
    update_hints();
}

void NavBar::create_shortcut_hints() {
    shortcut_bar_ = lv_obj_create(core_obj_);
    lv_obj_remove_style_all(shortcut_bar_);
    lv_obj_set_size(shortcut_bar_, LV_PCT(100), LV_PCT(100));
    lv_obj_align(shortcut_bar_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_flex_flow(shortcut_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(shortcut_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(shortcut_bar_, 10, 0);
    lv_obj_remove_flag(shortcut_bar_, LV_OBJ_FLAG_SCROLLABLE);

    // 12px is the spec's floor for readable text, and the compact bar has no
    // room above it.
    const lv_font_t* key_font = assets_.load_font("inter-semibold.ttf", 12);
    if (!key_font) {
        key_font = &lv_font_montserrat_12;
    }
    const lv_font_t* text_font = assets_.load_font("inter-regular.ttf", 12);
    if (!text_font) {
        text_font = &lv_font_montserrat_12;
    }

    for (size_t index = 0; index < kHintCount; ++index) {
        auto* item = lv_obj_create(shortcut_bar_);
        lv_obj_remove_style_all(item);
        lv_obj_set_size(item, LV_SIZE_CONTENT, LV_PCT(100));
        lv_obj_set_flex_flow(item, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(item, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(item, 3, 0);
        lv_obj_remove_flag(item, LV_OBJ_FLAG_SCROLLABLE);

        auto* key = lv_label_create(item);
        lv_label_set_text(key, "");
        lv_obj_set_style_text_font(key, key_font, 0);
        hint_key_labels_[index] = key;

        auto* label = lv_label_create(item);
        lv_label_set_text(label, "");
        lv_obj_set_style_text_font(label, text_font, 0);
        reactive::bind_theme(label, view_model_.dark_mode_subject(), reactive::ThemeRole::Text);
        hint_text_labels_[index] = label;
    }
}

void NavBar::update_hints() {
    const auto page = view_model_.current_page();
    const auto colors = view::palette(view_model_.is_dark_mode());

    const auto& hints = page == model::AppPage::Receive    ? kReceiveHints
                        : page == model::AppPage::Send     ? kSendHints
                        : page == model::AppPage::Settings ? kSettingsHints
                        : page == model::AppPage::Compose  ? kComposeHints
                        : page == model::AppPage::Files    ? kFilesHints
                                                           : kNoHints;

    for (size_t index = 0; index < kHintCount; ++index) {
        if (hint_key_labels_[index]) {
            // Hidden, not just blank: an empty item would still hold a flex
            // slot and push the real hints toward the middle.
            auto* item = lv_obj_get_parent(hint_key_labels_[index]);
            if (hints[index].key[0] == '\0') {
                lv_obj_add_flag(item, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_remove_flag(item, LV_OBJ_FLAG_HIDDEN);
            }
            lv_label_set_text(hint_key_labels_[index], hints[index].key);
            lv_obj_set_style_text_color(hint_key_labels_[index], colors.primary, 0);
        }
        if (hint_text_labels_[index]) {
            lv_label_set_text(hint_text_labels_[index], hints[index].action);
        }
    }
}

void NavBar::update_hints_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);

    auto* nav_bar = static_cast<NavBar*>(lv_observer_get_user_data(observer));
    if (nav_bar) {
        nav_bar->update_hints();
    }
}

} // namespace view::widgets
