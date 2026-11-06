/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "settings_screen.h"

#include "bindings.h"
#include "linux_input.h"
#include "theme.h"
#include "zerosend_service.h"

#include <cstdlib>

namespace screen {
namespace {

constexpr int kRowHeight = 20;
constexpr std::size_t kMaxInput = 120;

const char* row_label(int row) {
    static constexpr const char* kLabels[] = {
        "Device name", "Quick Save", "Save to", "Port", "Dark mode", "Encryption", "Fingerprint", "About",
    };
    return kLabels[row];
}

// Drops the last UTF-8 character, not the last byte.
void pop_utf8(std::string& text) {
    while (!text.empty()) {
        const auto byte = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((byte & 0xC0) != 0x80) {
            break;
        }
    }
}

} // namespace

SettingsScreen::SettingsScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : TabScreen(view_model, assets) {
    init();
}

SettingsScreen::~SettingsScreen() {
    if (editing_) {
        platform::set_text_input_mode(false);
    }
}

void SettingsScreen::build_content(lv_obj_t* content) {
    font_body_ = font("inter-regular.ttf", 12, &lv_font_montserrat_12);
    font_small_ = font("inter-regular.ttf", 11, &lv_font_montserrat_12);

    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(list_, 4, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    reactive::observe_obj(content, net::Service::instance().revision_subject(), changed_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), changed_cb, this);
    install_key_handler();
    render();
}

void SettingsScreen::changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<SettingsScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->render();
    }
}

bool SettingsScreen::editable(Row row) {
    return row == Row::Alias || row == Row::Destination || row == Row::Port;
}

std::string SettingsScreen::value_of(Row row) {
    const auto& service = net::Service::instance();
    const auto& state = service.state();
    const auto& options = service.options();
    switch (row) {
        case Row::Alias:
            return !options.alias.empty() ? options.alias : state.alias;
        case Row::QuickSave:
            return options.quick_save ? "On  (accept everything)" : "Off  (ask first)";
        case Row::Destination:
            return !options.destination.empty() ? options.destination : state.destination;
        case Row::Port:
            return std::to_string(options.port);
        case Row::DarkMode:
            return view_model().is_dark_mode() ? "On" : "Off";
        case Row::Encryption:
            return "HTTPS, always on";
        case Row::Fingerprint:
            return state.fingerprint.size() > 16 ? state.fingerprint.substr(0, 16) + "..." : state.fingerprint;
        case Row::About:
            return "ZeroSend " APP_VERSION " on LocalSend core";
        default:
            return {};
    }
}

void SettingsScreen::render() {
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_clean(list_);

    for (int index = 0; index < static_cast<int>(Row::Count); ++index) {
        const auto row_kind = static_cast<Row>(index);
        const bool active = index == cursor_;
        const bool info = row_kind == Row::Encryption || row_kind == Row::Fingerprint || row_kind == Row::About;

        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_color(row, colors.button, 0);
        lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(row, colors.primary, 0);
        lv_obj_set_style_border_width(row, active && editing_ ? 1 : 0, 0);

        auto* label = lv_label_create(row);
        lv_label_set_text(label, row_label(index));
        lv_obj_set_style_text_font(label, font_small_, 0);
        lv_obj_set_style_text_color(label, active ? colors.primary : colors.text, 0);
        lv_obj_set_style_text_opa(label, info && !active ? LV_OPA_60 : LV_OPA_COVER, 0);
        lv_obj_align(label, LV_ALIGN_LEFT_MID, 6, 0);

        auto* value = lv_label_create(row);
        const auto text = active && editing_ ? input_ + "_" : value_of(row_kind);
        lv_label_set_text(value, text.c_str());
        lv_label_set_long_mode(value, active && editing_ ? LV_LABEL_LONG_CLIP : LV_LABEL_LONG_DOT);
        lv_obj_set_width(value, 205);
        lv_obj_set_style_text_align(value, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_set_style_text_font(value, font_body_, 0);
        lv_obj_set_style_text_color(value, colors.text, 0);
        lv_obj_set_style_text_opa(value, info ? LV_OPA_60 : LV_OPA_COVER, 0);
        lv_obj_align(value, LV_ALIGN_RIGHT_MID, -6, 0);

        if (active) {
            lv_obj_update_layout(list_);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }
}

void SettingsScreen::activate() {
    auto& service = net::Service::instance();
    const auto row = static_cast<Row>(cursor_);
    if (editable(row)) {
        editing_ = true;
        input_ = value_of(row);
        platform::set_text_input_mode(true);
        render();
        return;
    }
    if (row == Row::QuickSave) {
        auto options = service.options();
        options.quick_save = !options.quick_save;
        service.update_options(options);
    } else if (row == Row::DarkMode) {
        view_model().toggle_dark_mode();
    }
    render();
}

void SettingsScreen::commit_edit() {
    auto& service = net::Service::instance();
    auto options = service.options();
    switch (static_cast<Row>(cursor_)) {
        case Row::Alias:
            if (!input_.empty()) {
                options.alias = input_;
            }
            break;
        case Row::Destination:
            if (!input_.empty()) {
                options.destination = input_;
            }
            break;
        case Row::Port: {
            const int port = std::atoi(input_.c_str());
            if (port >= 1024 && port < 65536) {
                options.port = port;
            }
            break;
        }
        default:
            break;
    }
    stop_editing();
    service.update_options(options); // restarts the daemon when the name, folder or port changed
}

void SettingsScreen::stop_editing() {
    editing_ = false;
    input_.clear();
    platform::set_text_input_mode(false);
    render();
}

bool SettingsScreen::on_key(uint32_t key) {
    if (editing_) {
        switch (key) {
            case LV_KEY_ENTER:
                commit_edit();
                return true;
            case LV_KEY_ESC:
                stop_editing();
                return true;
            case LV_KEY_BACKSPACE:
            case LV_KEY_DEL:
                pop_utf8(input_);
                render();
                return true;
            default:
                break;
        }
        const bool digits_only = static_cast<Row>(cursor_) == Row::Port;
        if (key >= 32 && key < 127 && input_.size() < kMaxInput &&
            (!digits_only || (key >= '0' && key <= '9'))) {
            input_.push_back(static_cast<char>(key));
            render();
        }
        return true; // editing owns the keyboard, digits included
    }

    switch (key) {
        case LV_KEY_UP:
            if (cursor_ > 0) {
                --cursor_;
                render();
            }
            return true;
        case LV_KEY_DOWN:
            if (cursor_ + 1 < static_cast<int>(Row::Count)) {
                ++cursor_;
                render();
            }
            return true;
        case LV_KEY_ENTER:
            activate();
            return true;
        default:
            return false;
    }
}

} // namespace screen
