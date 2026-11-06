/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "transfer_overlay.h"

#include "asset_manager.h"
#include "format.h"
#include "linux_input.h"
#include "theme.h"
#include "zerosend_service.h"

#include <string>

namespace view::widgets {
namespace {

constexpr uint32_t kAutoCloseMs = 2500;

const lv_font_t* load(app::AssetManager& assets, const char* file, uint32_t size, const lv_font_t* fallback) {
    const lv_font_t* font = assets.load_font(file, size);
    return font ? font : fallback;
}

} // namespace

TransferOverlay::TransferOverlay(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : view_model_(view_model) {
    // A full-screen scrim on the top layer, so the card reads as modal.
    root_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_50, 0);
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);

    card_ = lv_obj_create(root_);
    lv_obj_remove_style_all(card_);
    lv_obj_set_size(card_, 292, 112);
    lv_obj_center(card_);
    lv_obj_set_style_radius(card_, 8, 0);
    lv_obj_set_style_bg_opa(card_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card_, 1, 0);
    lv_obj_set_style_pad_all(card_, 8, 0);
    lv_obj_remove_flag(card_, LV_OBJ_FLAG_SCROLLABLE);

    icon_ = lv_label_create(card_);
    lv_obj_set_style_text_font(icon_, load(assets, "Phosphor-Fill.ttf", 20, &lv_font_montserrat_20), 0);
    lv_obj_align(icon_, LV_ALIGN_TOP_LEFT, 0, 0);

    title_ = lv_label_create(card_);
    lv_obj_set_style_text_font(title_, load(assets, "inter-semibold.ttf", 14, &lv_font_montserrat_14), 0);
    lv_label_set_long_mode(title_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title_, 244);
    lv_obj_align(title_, LV_ALIGN_TOP_LEFT, 28, 2);

    body_ = lv_label_create(card_);
    lv_obj_set_style_text_font(body_, load(assets, "inter-regular.ttf", 12, &lv_font_montserrat_12), 0);
    lv_label_set_long_mode(body_, LV_LABEL_LONG_WRAP);
    lv_obj_set_size(body_, 274, 46);
    lv_obj_align(body_, LV_ALIGN_TOP_LEFT, 0, 26);

    bar_ = lv_bar_create(card_);
    lv_obj_set_size(bar_, 274, 6);
    lv_obj_align(bar_, LV_ALIGN_BOTTOM_LEFT, 0, -20);
    lv_bar_set_range(bar_, 0, 1000);

    hint_ = lv_label_create(card_);
    lv_obj_set_style_text_font(hint_, load(assets, "inter-semibold.ttf", 12, &lv_font_montserrat_12), 0);
    lv_obj_align(hint_, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    service_observer_ =
        lv_subject_add_observer_obj(net::Service::instance().revision_subject(), changed_cb, root_, this);
    theme_observer_ = lv_subject_add_observer_obj(view_model_.dark_mode_subject(), changed_cb, root_, this);
    platform::set_modal_key_handler(key_handler, this);
    render();
}

TransferOverlay::~TransferOverlay() {
    platform::set_modal_key_handler(nullptr, nullptr);
    if (auto_close_) {
        lv_timer_delete(auto_close_);
    }
    if (root_ && lv_obj_is_valid(root_)) {
        lv_obj_delete(root_); // also removes the observers bound to it
    }
}

void TransferOverlay::changed_cb(lv_observer_t* observer, lv_subject_t* subject) {
    LV_UNUSED(subject);
    auto* self = static_cast<TransferOverlay*>(lv_observer_get_user_data(observer));
    if (self) {
        self->render();
    }
}

void TransferOverlay::auto_close_cb(lv_timer_t* timer) {
    auto* self = static_cast<TransferOverlay*>(lv_timer_get_user_data(timer));
    self->auto_close_ = nullptr; // one-shot: LVGL deletes it after this call
    net::Service::instance().dismiss_transfer();
}

void TransferOverlay::render() {
    const auto& state = net::Service::instance().state();
    const auto colors = view::palette(view_model_.is_dark_mode());

    lv_obj_set_style_bg_color(card_, colors.surface, 0);
    lv_obj_set_style_border_color(card_, colors.border, 0);
    lv_obj_set_style_text_color(title_, colors.text, 0);
    lv_obj_set_style_text_color(body_, colors.text, 0);
    lv_obj_set_style_text_color(hint_, colors.primary, 0);
    lv_obj_set_style_bg_color(bar_, colors.button, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_, colors.primary, LV_PART_INDICATOR);
    lv_obj_add_flag(bar_, LV_OBJ_FLAG_HIDDEN);

    const net::Transfer* transfer = state.transfer ? &*state.transfer : nullptr;
    if (state.request) {
        mode_ = Mode::Request;
    } else if (transfer && transfer->phase != net::Transfer::Phase::Done) {
        mode_ = Mode::Progress;
    } else if (transfer) {
        mode_ = Mode::Result;
    } else if (state.popup) {
        mode_ = Mode::Message;
    } else {
        mode_ = Mode::Hidden;
    }

    if (mode_ == Mode::Hidden) {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(root_, LV_OBJ_FLAG_HIDDEN);

    if (mode_ == Mode::Request) {
        const auto& request = *state.request;
        lv_label_set_text(icon_, view::ICON_DOWNLOAD);
        lv_obj_set_style_text_color(icon_, colors.primary, 0);
        lv_label_set_text_fmt(title_, "%s wants to send", request.alias.c_str());
        std::string body = std::to_string(request.count) + (request.count == 1 ? " file" : " files") +
                           "  ·  " + view::format_bytes(request.total);
        for (std::size_t i = 0; i < request.files.size() && i < 2; ++i) {
            body += "\n" + request.files[i].name;
        }
        if (request.files.size() > 2) {
            body += "  +" + std::to_string(request.count - 2) + " more";
        }
        lv_label_set_text(body_, body.c_str());
        lv_label_set_text(hint_, "OK Accept     P Always     ESC Decline");
        return;
    }

    if (mode_ == Mode::Progress) {
        const bool sending = transfer->direction == net::Transfer::Direction::Send;
        lv_label_set_text(icon_, sending ? view::ICON_PAPER_PLANE : view::ICON_DOWNLOAD);
        lv_obj_set_style_text_color(icon_, colors.primary, 0);
        std::string body;
        if (transfer->phase == net::Transfer::Phase::Waiting && transfer->is_text) {
            // The official apps show a text at once and only answer when their
            // dialog is closed, so "accept" would be the wrong thing to ask for.
            lv_label_set_text_fmt(title_, "Text to %s", transfer->alias.c_str());
            body = "Showing on " + transfer->alias + ". Done when it is closed there.";
        } else if (transfer->phase == net::Transfer::Phase::Waiting) {
            lv_label_set_text_fmt(title_, "Waiting for %s", transfer->alias.c_str());
            body = "Accept the request on " + transfer->alias + ".";
        } else {
            lv_label_set_text_fmt(title_, "%s %s", sending ? "Sending to" : "Receiving from", transfer->alias.c_str());
            body = view::format_bytes(transfer->done) + " of " + view::format_bytes(transfer->total);
            if (transfer->speed > 0) {
                body += "  ·  " + view::format_bytes(transfer->speed) + "/s";
            }
            lv_obj_remove_flag(bar_, LV_OBJ_FLAG_HIDDEN);
            const auto permille =
                transfer->total ? static_cast<int32_t>(transfer->done * 1000 / transfer->total) : 0;
            lv_bar_set_value(bar_, permille, LV_ANIM_OFF);
        }
        lv_label_set_text(body_, body.c_str());
        lv_label_set_text(hint_, "ESC Cancel");
        return;
    }

    if (mode_ == Mode::Result) {
        lv_label_set_text(icon_, transfer->ok ? view::ICON_CHECK_CIRCLE : view::ICON_X_CIRCLE);
        lv_obj_set_style_text_color(icon_, transfer->ok ? colors.primary : colors.error, 0);
        const bool sending = transfer->direction == net::Transfer::Direction::Send;
        lv_label_set_text(title_, transfer->ok ? (sending ? (transfer->is_text ? "Delivered" : "Sent") : "Received")
                                               : "Not completed");
        std::string body = transfer->result;
        if (!sending && transfer->ok && !state.destination.empty()) {
            body += "\nSaved to " + state.destination;
        }
        lv_label_set_text(body_, body.c_str());
        lv_label_set_text(hint_, "OK Close");
        if (transfer->ok && !auto_close_) {
            auto_close_ = lv_timer_create(auto_close_cb, kAutoCloseMs, this);
            lv_timer_set_repeat_count(auto_close_, 1);
        }
        return;
    }

    // A received text.
    const auto& message = *state.popup;
    lv_label_set_text(icon_, view::ICON_CHAT_TEXT);
    lv_obj_set_style_text_color(icon_, colors.primary, 0);
    lv_label_set_text_fmt(title_, "Text from %s", message.from.c_str());
    lv_label_set_text(body_, message.text.c_str());
    lv_label_set_text(hint_, "OK Close     (kept in Receive)");
}

bool TransferOverlay::key_handler(uint32_t key, void* user_data) {
    auto* self = static_cast<TransferOverlay*>(user_data);
    return self && self->on_key(key);
}

bool TransferOverlay::on_key(uint32_t key) {
    auto& service = net::Service::instance();
    switch (mode_) {
        case Mode::Hidden:
            return false;
        case Mode::Request:
            if (key == LV_KEY_ENTER || key == 'y' || key == 'Y') {
                service.answer(true, false);
            } else if (key == 'p' || key == 'P') {
                service.answer(true, true);
            } else if (key == LV_KEY_ESC || key == 'n' || key == 'N') {
                service.answer(false, false);
            }
            return true;
        case Mode::Progress:
            if (key == LV_KEY_ESC) {
                service.cancel();
            }
            return true;
        case Mode::Result:
            if (key == LV_KEY_ENTER || key == LV_KEY_ESC) {
                if (auto_close_) {
                    lv_timer_delete(auto_close_);
                    auto_close_ = nullptr;
                }
                service.dismiss_transfer();
            }
            return true;
        case Mode::Message:
            if (key == LV_KEY_ENTER || key == LV_KEY_ESC) {
                service.dismiss_popup();
            }
            return true;
    }
    return false;
}

} // namespace view::widgets
