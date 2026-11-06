/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "subjects.h"
#include "base_model.h"

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

namespace viewmodel {

class BaseViewModel {
public:
    BaseViewModel();
    ~BaseViewModel();

    BaseViewModel(const BaseViewModel&) = delete;
    BaseViewModel& operator=(const BaseViewModel&) = delete;

    lv_subject_t* title_subject();
    lv_subject_t* dark_mode_subject();
    lv_subject_t* current_page_subject();
    lv_subject_t* quit_requested_subject();

    bool is_dark_mode() const;
    void set_dark_mode(bool enabled);
    void toggle_dark_mode();

    model::AppPage current_page() const;
    void open_page(model::AppPage page);
    // Back from a sub-page: to the Send tab, which every sub-page comes from.
    void go_home();
    void next_tab();
    void previous_tab();

    // Compose straight to one device (Enter on a device, nothing selected),
    // or into the selection (T on the Send tab).
    void compose_to(const std::string& fingerprint, const std::string& alias);
    void compose_selection();
    const std::string& target_fingerprint() const;
    const std::string& target_alias() const;

    // The Send tab's selection. Setting one kind clears the other.
    const model::Selection& selection() const;
    void select_text(const std::string& text);
    void select_files(const std::vector<std::string>& paths, uint64_t total_bytes);
    void clear_selection();
    lv_subject_t* selection_subject();

    int selected_device() const;
    void set_selected_device(int index);

    void request_quit();

private:
    void publish_all();

    model::BaseModel model_;
    reactive::StringSubject<48> title_subject_;
    reactive::BoolSubject dark_mode_subject_;
    reactive::IntSubject current_page_subject_;
    reactive::BoolSubject quit_requested_subject_;
    reactive::IntSubject selection_subject_; // bumped on every selection change
};

} // namespace viewmodel
