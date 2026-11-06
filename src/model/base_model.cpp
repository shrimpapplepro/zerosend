/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "base_model.h"

#include <utility>

namespace model {

const char* page_title(AppPage page) {
    switch (page) {
        case AppPage::Compose:
            return "Text";
        case AppPage::Files:
            return "Pick files";
        default:
            return tab_title(static_cast<int>(page));
    }
}

const char* tab_title(int tab) {
    switch (tab) {
        case 0:
            return "Receive";
        case 1:
            return "Send";
        case 2:
            return "Settings";
        default:
            return "";
    }
}

const char* BaseModel::app_title() const {
    return "ZeroSend";
}

bool BaseModel::dark_mode() const {
    return dark_mode_;
}

void BaseModel::set_dark_mode(bool enabled) {
    dark_mode_ = enabled;
}

void BaseModel::toggle_dark_mode() {
    dark_mode_ = !dark_mode_;
}

AppPage BaseModel::current_page() const {
    return current_page_;
}

void BaseModel::set_current_page(AppPage page) {
    current_page_ = page;
}

const std::string& BaseModel::target_fingerprint() const {
    return target_fingerprint_;
}

const std::string& BaseModel::target_alias() const {
    return target_alias_;
}

void BaseModel::set_target(std::string fingerprint, std::string alias) {
    target_fingerprint_ = std::move(fingerprint);
    target_alias_ = std::move(alias);
}

int BaseModel::selected_device() const {
    return selected_device_;
}

void BaseModel::set_selected_device(int index) {
    selected_device_ = index < 0 ? 0 : index;
}

} // namespace model
