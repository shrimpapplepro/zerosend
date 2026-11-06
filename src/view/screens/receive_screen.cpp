/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "receive_screen.h"

#include "bindings.h"
#include "format.h"
#include "theme.h"
#include "zerosend_service.h"

#include <string>

namespace screen {
namespace {

constexpr int kHeaderHeight = 40;
constexpr int kRowHeight = 19;

} // namespace

ReceiveScreen::ReceiveScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : TabScreen(view_model, assets) {
    init();
}

void ReceiveScreen::build_content(lv_obj_t* content) {
    content_ = content;
    font_big_ = font("inter-semibold.ttf", 16, &lv_font_montserrat_16);
    font_body_ = font("inter-regular.ttf", 12, &lv_font_montserrat_12);
    font_small_ = font("inter-regular.ttf", 11, &lv_font_montserrat_12);
    font_icon_ = font("Phosphor-Fill.ttf", 14, &lv_font_montserrat_14);

    alias_ = lv_label_create(content);
    lv_obj_set_style_text_font(alias_, font_big_, 0);
    lv_label_set_long_mode(alias_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(alias_, 210);
    lv_obj_align(alias_, LV_ALIGN_TOP_LEFT, 8, 3);

    address_ = lv_label_create(content);
    lv_obj_set_style_text_font(address_, font_small_, 0);
    lv_label_set_long_mode(address_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(address_, 300);
    lv_obj_align(address_, LV_ALIGN_TOP_LEFT, 8, 23);

    // Status pill, top right: Ready / Quick Save / Starting / Error.
    status_ = lv_label_create(content);
    lv_obj_set_style_text_font(status_, font_small_, 0);
    lv_obj_set_style_radius(status_, 8, 0);
    lv_obj_set_style_pad_hor(status_, 7, 0);
    lv_obj_set_style_pad_ver(status_, 2, 0);
    lv_obj_set_style_bg_opa(status_, LV_OPA_COVER, 0);
    lv_obj_align(status_, LV_ALIGN_TOP_RIGHT, -6, 5);

    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), 120 - kHeaderHeight); // content is 320x120
    lv_obj_align(list_, LV_ALIGN_TOP_LEFT, 0, kHeaderHeight);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(list_, 4, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    detail_ = lv_obj_create(content);
    lv_obj_remove_style_all(detail_);
    lv_obj_set_size(detail_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(detail_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(detail_, 6, 0);
    lv_obj_set_flex_flow(detail_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(detail_, 3, 0);
    lv_obj_set_scroll_dir(detail_, LV_DIR_VER);
    lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);

    auto& service = net::Service::instance();
    reactive::observe_obj(content, service.revision_subject(), changed_cb, this);
    reactive::observe_obj(content, view_model().dark_mode_subject(), changed_cb, this);
    install_key_handler();
    render();
}

void ReceiveScreen::changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<ReceiveScreen*>(lv_observer_get_user_data(observer));
    if (self) {
        self->render();
    }
}

void ReceiveScreen::render() {
    auto& service = net::Service::instance();
    const auto& state = service.state();
    const auto colors = view::palette(view_model().is_dark_mode());

    // Header: this device as the others see it.
    lv_label_set_text(alias_, state.alias.empty() ? "ZeroSend" : state.alias.c_str());
    lv_obj_set_style_text_color(alias_, colors.text, 0);

    std::string address;
    if (state.status == net::ServiceState::Status::Failed) {
        address = state.status_text;
    } else if (state.status == net::ServiceState::Status::Starting) {
        address = state.status_text + "...";
    } else {
        for (const auto& ip : state.ips) {
            if (ip.rfind("100.", 0) == 0 && state.ips.size() > 1) {
                continue; // Tailscale: LocalSend peers are on the LAN
            }
            address += (address.empty() ? "" : "  ") + ip;
            break;
        }
        address += "  port " + std::to_string(state.port);
        if (!state.multicast) {
            address += "  (no multicast)";
        }
    }
    lv_label_set_text(address_, address.c_str());
    lv_obj_set_style_text_color(address_,
                                state.status == net::ServiceState::Status::Failed ? colors.error : colors.text,
                                0);
    lv_obj_set_style_text_opa(address_, LV_OPA_80, 0);

    const char* pill = "Ready";
    lv_color_t pill_color = colors.primary;
    switch (state.status) {
        case net::ServiceState::Status::Starting:
            pill = "Starting";
            pill_color = colors.warning;
            break;
        case net::ServiceState::Status::Failed:
            pill = "Offline";
            pill_color = colors.error;
            break;
        case net::ServiceState::Status::Ready:
            pill = state.quick_save ? "Quick Save" : "Ready";
            break;
    }
    lv_label_set_text(status_, pill);
    lv_obj_set_style_bg_color(status_, pill_color, 0);
    lv_obj_set_style_text_color(status_, colors.background, 0);

    // History.
    lv_obj_clean(list_);
    const auto& inbox = state.inbox;
    if (cursor_ >= static_cast<int>(inbox.size())) {
        cursor_ = inbox.empty() ? 0 : static_cast<int>(inbox.size()) - 1;
    }
    if (inbox.empty()) {
        auto* empty = lv_label_create(list_);
        lv_label_set_text(empty, "Nothing received yet.\nOpen LocalSend on another device on this Wi-Fi.");
        lv_obj_set_style_text_font(empty, font_small_, 0);
        lv_obj_set_style_text_color(empty, colors.text, 0);
        lv_obj_set_style_text_opa(empty, LV_OPA_60, 0);
        lv_obj_set_width(empty, LV_PCT(100));
        lv_obj_set_style_pad_top(empty, 8, 0);
        lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, 0);
        lv_label_set_long_mode(empty, LV_LABEL_LONG_WRAP);
    }
    for (int index = 0; index < static_cast<int>(inbox.size()); ++index) {
        const auto& item = inbox[index];
        const bool active = index == cursor_;

        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_bg_color(row, colors.button, 0);
        lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        auto* icon = lv_label_create(row);
        lv_label_set_text(icon, item.is_message ? view::ICON_CHAT_TEXT : view::ICON_DOWNLOAD);
        lv_obj_set_style_text_font(icon, font_icon_, 0);
        lv_obj_set_style_text_color(icon, item.unread ? colors.primary : colors.text, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 4, 0);

        auto* text = lv_label_create(row);
        const auto line = item.from + ": " + view::first_line(item.text, 80);
        lv_label_set_text(text, line.c_str());
        lv_label_set_long_mode(text, LV_LABEL_LONG_DOT);
        lv_obj_set_width(text, 250);
        lv_obj_set_style_text_font(text, font_body_, 0);
        lv_obj_set_style_text_color(text, active ? colors.primary : colors.text, 0);
        lv_obj_align(text, LV_ALIGN_LEFT_MID, 24, 0);

        auto* age = lv_label_create(row);
        lv_label_set_text(age, view::format_age(item.received_at).c_str());
        lv_obj_set_style_text_font(age, font_small_, 0);
        lv_obj_set_style_text_color(age, colors.text, 0);
        lv_obj_set_style_text_opa(age, LV_OPA_60, 0);
        lv_obj_align(age, LV_ALIGN_RIGHT_MID, -4, 0);

        if (active) {
            lv_obj_update_layout(list_);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }

    // Seeing the tab is reading it: the badge in the overlay counts unseen items.
    if (service.unread_count() > 0 && !detail_open_) {
        // Deferred, since marking read bumps the revision this render answers.
        lv_async_call([](void*) { net::Service::instance().mark_inbox_read(); }, nullptr);
    }

    if (detail_open_) {
        render_detail();
    }
}

void ReceiveScreen::render_detail() {
    const auto& inbox = net::Service::instance().state().inbox;
    const auto colors = view::palette(view_model().is_dark_mode());
    lv_obj_set_style_bg_color(detail_, colors.surface, 0);
    lv_obj_clean(detail_);
    if (inbox.empty() || cursor_ >= static_cast<int>(inbox.size())) {
        detail_open_ = false;
        lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const auto& item = inbox[cursor_];

    auto* header = lv_label_create(detail_);
    const auto from = std::string(item.is_message ? "Text from " : "Files from ") + item.from + "  ·  " +
                      view::format_age(item.received_at);
    lv_label_set_text(header, from.c_str());
    lv_obj_set_style_text_font(header, font_small_, 0);
    lv_obj_set_style_text_color(header, colors.primary, 0);

    auto* body = lv_label_create(detail_);
    std::string text = item.text;
    if (!item.is_message) {
        for (const auto& path : item.paths) {
            text += "\n" + path;
        }
    }
    lv_label_set_text(body, text.c_str());
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, LV_PCT(100));
    lv_obj_set_style_text_font(body, item.is_message ? font_big_ : font_small_, 0);
    lv_obj_set_style_text_color(body, colors.text, 0);
}

bool ReceiveScreen::on_key(uint32_t key) {
    auto& service = net::Service::instance();
    const int count = static_cast<int>(service.state().inbox.size());

    if (detail_open_) {
        switch (key) {
            case LV_KEY_ESC:
            case LV_KEY_ENTER:
                detail_open_ = false;
                lv_obj_add_flag(detail_, LV_OBJ_FLAG_HIDDEN);
                render();
                return true;
            case LV_KEY_UP:
                lv_obj_scroll_by_bounded(detail_, 0, 20, LV_ANIM_OFF);
                return true;
            case LV_KEY_DOWN:
                lv_obj_scroll_by_bounded(detail_, 0, -20, LV_ANIM_OFF);
                return true;
            default:
                return true; // the detail owns the screen while it is open
        }
    }

    switch (key) {
        case LV_KEY_UP:
            if (cursor_ > 0) {
                --cursor_;
                render();
            }
            return true;
        case LV_KEY_DOWN:
            if (cursor_ + 1 < count) {
                ++cursor_;
                render();
            }
            return true;
        case LV_KEY_ENTER:
            if (count > 0) {
                detail_open_ = true;
                lv_obj_remove_flag(detail_, LV_OBJ_FLAG_HIDDEN);
                render_detail();
            }
            return true;
        case LV_KEY_DEL:
        case LV_KEY_BACKSPACE:
            cursor_ = 0;
            service.clear_inbox();
            return true;
        default:
            return false;
    }
}

} // namespace screen
