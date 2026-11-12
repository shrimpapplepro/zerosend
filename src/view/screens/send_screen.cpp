/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "send_screen.h"

#include "bindings.h"
#include "format.h"
#include "theme.h"
#include "zerosend_service.h"

#include <filesystem>
#include <string>

namespace screen {
namespace {

constexpr int kSelectionHeight = 26;
constexpr int kHeadingHeight = 16;
constexpr int kRowHeight = 26;

} // namespace

SendScreen::SendScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : TabScreen(view_model, assets) {
    init();
}

void SendScreen::build_content(lv_obj_t* content) {
    font_title_ = font("inter-semibold.ttf", 13, &lv_font_montserrat_14);
    font_body_ = font("inter-regular.ttf", 12, &lv_font_montserrat_12);
    font_small_ = font("inter-regular.ttf", 10, &lv_font_montserrat_12);
    font_icon_ = font("Phosphor-Fill.ttf", 16, &lv_font_montserrat_16);

    // Selection card.
    selection_ = lv_obj_create(content);
    lv_obj_remove_style_all(selection_);
    lv_obj_set_size(selection_, 308, kSelectionHeight - 4);
    lv_obj_align(selection_, LV_ALIGN_TOP_MID, 0, 3);
    lv_obj_set_style_radius(selection_, 5, 0);
    lv_obj_set_style_bg_opa(selection_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(selection_, 1, 0);
    lv_obj_remove_flag(selection_, LV_OBJ_FLAG_SCROLLABLE);

    selection_icon_ = lv_label_create(selection_);
    lv_obj_set_style_text_font(selection_icon_, font_icon_, 0);
    lv_obj_align(selection_icon_, LV_ALIGN_LEFT_MID, 6, 0);

    selection_text_ = lv_label_create(selection_);
    lv_obj_set_style_text_font(selection_text_, font_body_, 0);
    lv_label_set_long_mode(selection_text_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(selection_text_, 270);
    lv_obj_align(selection_text_, LV_ALIGN_LEFT_MID, 28, 0);

    heading_ = lv_label_create(content);
    lv_label_set_text(heading_, "Nearby devices");
    lv_obj_set_style_text_font(heading_, font_small_, 0);
    lv_obj_align(heading_, LV_ALIGN_TOP_LEFT, 8, kSelectionHeight + 1);

    scanning_ = lv_label_create(content);
    lv_obj_set_style_text_font(scanning_, font_small_, 0);
    lv_obj_align(scanning_, LV_ALIGN_TOP_RIGHT, -8, kSelectionHeight + 1);

    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), 120 - kSelectionHeight - kHeadingHeight);
    lv_obj_align(list_, LV_ALIGN_TOP_LEFT, 0, kSelectionHeight + kHeadingHeight);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(list_, 4, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    auto& service = net::Service::instance();
    reactive::observe_obj(content, service.revision_subject(), changed_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), changed_cb, this);
    reactive::observe_obj(content, view_model().selection_subject(), changed_cb, this);
    install_key_handler();
    render();
}

void SendScreen::changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<SendScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->render();
    }
}

void SendScreen::render() {
    const auto& state = net::Service::instance().state();
    const auto& selection = view_model().selection();
    const auto colors = view::palette(view_model().is_dark_mode());

    lv_obj_set_style_bg_color(selection_, colors.button, 0);
    lv_obj_set_style_border_color(selection_, selection.empty() ? colors.border : colors.primary, 0);
    std::string summary;
    if (!selection.text.empty()) {
        lv_label_set_text(selection_icon_, view::ICON_CHAT_TEXT);
        summary = "\"" + view::first_line(selection.text, 90) + "\"";
    } else if (!selection.paths.empty()) {
        lv_label_set_text(selection_icon_, selection.paths.size() == 1 ? view::ICON_FILE : view::ICON_FOLDER);
        if (selection.paths.size() == 1) {
            summary = std::filesystem::path(selection.paths.front()).filename().string() + "  ·  " +
                      view::format_bytes(selection.total_bytes);
        } else {
            summary = std::to_string(selection.paths.size()) + " items  ·  " +
                      view::format_bytes(selection.total_bytes);
        }
    } else {
        lv_label_set_text(selection_icon_, view::ICON_PAPER_PLANE);
        summary = "Nothing selected:  P files  ·  T text";
    }
    lv_label_set_text(selection_text_, summary.c_str());
    lv_obj_set_style_text_color(selection_text_, colors.text, 0);
    lv_obj_set_style_text_opa(selection_text_, selection.empty() ? LV_OPA_60 : LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(selection_icon_, selection.empty() ? colors.text : colors.primary, 0);

    lv_obj_set_style_text_color(heading_, colors.text, 0);
    lv_obj_set_style_text_opa(heading_, LV_OPA_60, 0);
    lv_label_set_text(scanning_, state.scanning ? "Scanning..." : "");
    lv_obj_set_style_text_color(scanning_, colors.primary, 0);

    lv_obj_clean(list_);
    const int count = static_cast<int>(state.devices.size());
    int cursor = view_model().selected_device();
    if (cursor >= count) {
        cursor = count > 0 ? count - 1 : 0;
        view_model().set_selected_device(cursor);
    }

    if (count == 0) {
        auto* empty = lv_label_create(list_);
        const char* text = state.status == net::ServiceState::Status::Failed
                               ? state.status_text.c_str()
                           : state.status == net::ServiceState::Status::Ready && state.ips.empty()
                               ? "Not on a network. Connect to Wi-Fi first;\nZeroSend picks it up by itself."
                           : state.scanning ? "Looking for LocalSend devices..."
                                            : "No devices found. Open LocalSend on the\nother device, then press R.";
        lv_label_set_text(empty, text);
        lv_obj_set_style_text_font(empty, font_body_, 0);
        lv_obj_set_style_text_color(
            empty, state.status == net::ServiceState::Status::Failed ? colors.error : colors.text, 0);
        lv_obj_set_style_text_opa(empty, LV_OPA_70, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_pad_top(empty, 10, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
        return;
    }

    for (int index = 0; index < count; ++index) {
        const auto& device = state.devices[index];
        const bool active = index == cursor;

        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_color(row, colors.button, 0);
        lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        auto* accent = lv_obj_create(row);
        lv_obj_remove_style_all(accent);
        lv_obj_set_size(accent, 3, 16);
        lv_obj_align(accent, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_bg_color(accent, colors.primary, 0);
        lv_obj_set_style_bg_opa(accent, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        auto* icon = lv_label_create(row);
        lv_label_set_text(icon, view::device_icon(device.type));
        lv_obj_set_style_text_font(icon, font_icon_, 0);
        lv_obj_set_style_text_color(icon, active ? colors.primary : colors.text, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 9, 0);

        auto* name = lv_label_create(row);
        lv_label_set_text(name, device.alias.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 240);
        lv_obj_set_style_text_font(name, font_title_, 0);
        lv_obj_set_style_text_color(name, active ? colors.primary : colors.text, 0);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 32, 1);

        auto* detail = lv_label_create(row);
        std::string line = device.model.empty() ? std::string{} : device.model + "  ·  ";
        line += device.host;
        lv_label_set_text(detail, line.c_str());
        lv_label_set_long_mode(detail, LV_LABEL_LONG_DOT);
        lv_obj_set_width(detail, 240);
        lv_obj_set_style_text_font(detail, font_small_, 0);
        lv_obj_set_style_text_color(detail, colors.text, 0);
        lv_obj_set_style_text_opa(detail, LV_OPA_60, 0);
        lv_obj_align(detail, LV_ALIGN_BOTTOM_LEFT, 32, -1);

        if (device.paired) {
            auto* pin = lv_label_create(row);
            lv_label_set_text(pin, view::ICON_PUSH_PIN);
            lv_obj_set_style_text_font(pin, font_icon_, 0);
            lv_obj_set_style_text_color(pin, colors.primary, 0);
            lv_obj_align(pin, LV_ALIGN_RIGHT_MID, -6, 0);
        }

        if (active) {
            lv_obj_update_layout(list_);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }
}

void SendScreen::send_to_selected() {
    auto& service = net::Service::instance();
    const auto& devices = service.state().devices;
    const int cursor = view_model().selected_device();
    if (cursor < 0 || cursor >= static_cast<int>(devices.size())) {
        return;
    }
    const auto& device = devices[cursor];
    const auto& selection = view_model().selection();
    if (selection.empty()) {
        view_model().compose_to(device.fingerprint, device.alias);
    } else if (!selection.text.empty()) {
        service.send_text(device.fingerprint, selection.text);
    } else {
        service.send_files(device.fingerprint, selection.paths);
    }
}

bool SendScreen::on_key(uint32_t key) {
    const int count = static_cast<int>(net::Service::instance().state().devices.size());
    const int cursor = view_model().selected_device();
    switch (key) {
        case LV_KEY_UP:
            if (cursor > 0) {
                view_model().set_selected_device(cursor - 1);
                render();
            }
            return true;
        case LV_KEY_DOWN:
            if (cursor + 1 < count) {
                view_model().set_selected_device(cursor + 1);
                render();
            }
            return true;
        case LV_KEY_ENTER:
            send_to_selected();
            return true;
        case 'p':
        case 'P':
            view_model().open_page(model::AppPage::Files);
            return true;
        case 't':
        case 'T':
            view_model().compose_selection();
            return true;
        case 'r':
        case 'R':
            net::Service::instance().refresh();
            return true;
        case LV_KEY_DEL:
        case LV_KEY_BACKSPACE:
            view_model().clear_selection();
            return true;
        default:
            return false;
    }
}

} // namespace screen
