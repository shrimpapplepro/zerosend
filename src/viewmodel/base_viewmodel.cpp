/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_viewmodel.h"

namespace viewmodel {
namespace {

int page_to_int(model::AppPage page) {
    return static_cast<int>(page);
}

} // namespace

BaseViewModel::BaseViewModel()
    : title_subject_(model_.app_title()),
      dark_mode_subject_(model_.dark_mode()),
      current_page_subject_(page_to_int(model_.current_page())),
      quit_requested_subject_(false),
      selection_subject_(0) {}

BaseViewModel::~BaseViewModel() = default;

lv_subject_t* BaseViewModel::title_subject() {
    return title_subject_.native();
}

lv_subject_t* BaseViewModel::dark_mode_subject() {
    return dark_mode_subject_.native();
}

lv_subject_t* BaseViewModel::current_page_subject() {
    return current_page_subject_.native();
}

lv_subject_t* BaseViewModel::quit_requested_subject() {
    return quit_requested_subject_.native();
}

bool BaseViewModel::is_dark_mode() const {
    return model_.dark_mode();
}

void BaseViewModel::set_dark_mode(bool enabled) {
    model_.set_dark_mode(enabled);
    publish_all();
}

void BaseViewModel::toggle_dark_mode() {
    model_.toggle_dark_mode();
    publish_all();
}

model::AppPage BaseViewModel::current_page() const {
    return model_.current_page();
}

void BaseViewModel::open_page(model::AppPage page) {
    model_.set_current_page(page);
    publish_all();
}

void BaseViewModel::go_home() {
    open_page(model::AppPage::Send);
}

void BaseViewModel::next_tab() {
    const int tab = static_cast<int>(model_.current_page());
    open_page(static_cast<model::AppPage>((tab + 1) % model::kTabCount));
}

void BaseViewModel::previous_tab() {
    const int tab = static_cast<int>(model_.current_page());
    open_page(static_cast<model::AppPage>((tab + model::kTabCount - 1) % model::kTabCount));
}

void BaseViewModel::compose_to(const std::string& fingerprint, const std::string& alias) {
    model_.set_target(fingerprint, alias);
    open_page(model::AppPage::Compose);
}

void BaseViewModel::compose_selection() {
    model_.set_target({}, {});
    open_page(model::AppPage::Compose);
}

const model::Selection& BaseViewModel::selection() const {
    return model_.selection();
}

void BaseViewModel::select_text(const std::string& text) {
    auto& selection = model_.selection();
    selection = model::Selection{};
    selection.text = text;
    selection_subject_.set(selection_subject_.value() + 1);
}

void BaseViewModel::select_files(const std::vector<std::string>& paths, uint64_t total_bytes) {
    auto& selection = model_.selection();
    selection = model::Selection{};
    selection.paths = paths;
    selection.total_bytes = total_bytes;
    selection_subject_.set(selection_subject_.value() + 1);
}

void BaseViewModel::clear_selection() {
    model_.selection() = model::Selection{};
    selection_subject_.set(selection_subject_.value() + 1);
}

lv_subject_t* BaseViewModel::selection_subject() {
    return selection_subject_.native();
}

const std::string& BaseViewModel::target_fingerprint() const {
    return model_.target_fingerprint();
}

const std::string& BaseViewModel::target_alias() const {
    return model_.target_alias();
}

int BaseViewModel::selected_device() const {
    return model_.selected_device();
}

void BaseViewModel::set_selected_device(int index) {
    model_.set_selected_device(index);
}

void BaseViewModel::request_quit() {
    quit_requested_subject_.set(true);
}

void BaseViewModel::publish_all() {
    title_subject_.set(model_.app_title());
    dark_mode_subject_.set(model_.dark_mode());
    current_page_subject_.set(page_to_int(model_.current_page()));
}

} // namespace viewmodel
