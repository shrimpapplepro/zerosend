/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

namespace app {
class AssetManager;
}

namespace view::widgets {

class TitleBar : public BaseWidgets {
public:
    TitleBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~TitleBar() override;

    void build() override;

private:
    void refresh_time();
    void refresh_device_status();
    static void refresh_timer_cb(lv_timer_t* timer);
    static void page_changed_cb(lv_observer_t* observer, lv_subject_t* subject);

    void refresh_title();

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    lv_obj_t* title_row_{nullptr};
    lv_obj_t* tab_row_{nullptr};
    lv_obj_t* tab_labels_[3]{};
    lv_obj_t* app_title_{nullptr};
    lv_obj_t* separator_{nullptr};
    lv_obj_t* page_title_{nullptr};
    lv_obj_t* time_label_{nullptr};
    lv_obj_t* wifi_label_{nullptr};
    lv_obj_t* ethernet_label_{nullptr};
    lv_obj_t* battery_icon_label_{nullptr};
    lv_obj_t* battery_percent_label_{nullptr};
    lv_timer_t* refresh_timer_{nullptr};
    const lv_font_t* font_app_large_{nullptr};
    const lv_font_t* font_app_small_{nullptr};
    const lv_font_t* font_page_{nullptr};
    uint32_t refresh_tick_{0};
};

} // namespace view::widgets
