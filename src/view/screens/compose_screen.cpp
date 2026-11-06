/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "compose_screen.h"

#include "asset_manager.h"
#include "linux_input.h"
#include "theme.h"
#include "zerosend_service.h"

namespace screen {
namespace {

// LocalSend sends up to 64 KB as an inline message; a handheld keyboard will
// never get near that, so this only bounds the label.
constexpr std::size_t kMaxInput = 2000;

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

ComposeScreen::ComposeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : ToolScreen(view_model, assets) {
    init();
}

ComposeScreen::~ComposeScreen() {
    platform::set_text_input_mode(false);
}

void ComposeScreen::build_content(lv_obj_t* content) {
    auto* font_body = assets().load_font("inter-regular.ttf", 13);
    auto* font_small = assets().load_font("inter-regular.ttf", 11);
    const lv_font_t* body = font_body ? font_body : &lv_font_montserrat_14;
    const lv_font_t* small = font_small ? font_small : &lv_font_montserrat_12;

    // Editing the selection starts from what is already selected.
    if (view_model().target_fingerprint().empty()) {
        input_ = view_model().selection().text;
    }

    to_ = lv_label_create(content);
    lv_obj_set_style_text_font(to_, small, 0);
    lv_label_set_long_mode(to_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(to_, 230);
    lv_obj_align(to_, LV_ALIGN_TOP_LEFT, 8, 3);

    counter_ = lv_label_create(content);
    lv_obj_set_style_text_font(counter_, small, 0);
    lv_obj_align(counter_, LV_ALIGN_TOP_RIGHT, -8, 3);

    box_ = lv_obj_create(content);
    lv_obj_remove_style_all(box_);
    lv_obj_set_size(box_, 304, 96);
    lv_obj_align(box_, LV_ALIGN_TOP_MID, 0, 19);
    lv_obj_set_style_radius(box_, 5, 0);
    lv_obj_set_style_border_width(box_, 1, 0);
    lv_obj_set_style_bg_opa(box_, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(box_, 5, 0);
    lv_obj_set_scroll_dir(box_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(box_, LV_SCROLLBAR_MODE_OFF);

    text_ = lv_label_create(box_);
    lv_obj_set_style_text_font(text_, body, 0);
    lv_label_set_long_mode(text_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(text_, LV_PCT(100));

    install_key_handler();
    platform::set_text_input_mode(true); // F/Z/X/C are letters here
    render();
}

void ComposeScreen::render() {
    const auto colors = view::palette(view_model().is_dark_mode());
    const auto& target = view_model().target_alias();
    const std::string to = target.empty() ? "Text for the selection" : "To " + target;
    lv_label_set_text(to_, to.c_str());
    lv_obj_set_style_text_color(to_, colors.primary, 0);

    lv_label_set_text_fmt(counter_, "%u", static_cast<unsigned>(input_.size()));
    lv_obj_set_style_text_color(counter_, colors.text, 0);
    lv_obj_set_style_text_opa(counter_, LV_OPA_50, 0);

    lv_obj_set_style_bg_color(box_, colors.button, 0);
    lv_obj_set_style_border_color(box_, colors.primary, 0);
    const auto shown = input_ + "_";
    lv_label_set_text(text_, shown.c_str());
    lv_obj_set_style_text_color(text_, colors.text, 0);
    lv_obj_update_layout(box_);
    lv_obj_scroll_to_y(box_, LV_COORD_MAX, LV_ANIM_OFF);
}

bool ComposeScreen::on_key(uint32_t key) {
    switch (key) {
        case LV_KEY_ENTER: {
            if (input_.empty()) {
                return true;
            }
            const auto& fingerprint = view_model().target_fingerprint();
            if (fingerprint.empty()) {
                view_model().select_text(input_);
            } else {
                net::Service::instance().send_text(fingerprint, input_);
            }
            view_model().go_home();
            return true;
        }
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            pop_utf8(input_);
            render();
            return true;
        default:
            break;
    }
    if (key >= 32 && key < 127 && input_.size() < kMaxInput) {
        input_.push_back(static_cast<char>(key));
        render();
        return true;
    }
    return true; // arrows and the rest do nothing here, and must not leak
}

} // namespace screen
