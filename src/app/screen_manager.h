/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asset_manager.h"
#include "base_screen.h"
#include "base_viewmodel.h"
#include "lvgl.h"

#include <memory>

namespace app {

class ScreenManager {
public:
    ScreenManager(viewmodel::BaseViewModel& view_model, AssetManager& assets);
    ~ScreenManager();

    ScreenManager(const ScreenManager&) = delete;
    ScreenManager& operator=(const ScreenManager&) = delete;

    void start();
    void request_page(model::AppPage page);
    // Rebuild the current page even though it has not changed. The desktop
    // launcher needs this to hand control back to a freshly built screen.
    void reload_current_page();
    void flush_requested_page();
    lv_obj_t* current_screen() const;

private:
    void show_page(model::AppPage page);
    void load_screen(std::unique_ptr<screen::BaseScreen> next_screen);

    viewmodel::BaseViewModel& view_model_;
    AssetManager& assets_;
    std::unique_ptr<screen::BaseScreen> current_screen_;
    lv_observer_t* page_observer_ = nullptr;
    model::AppPage requested_page_ = model::AppPage::Receive;
    model::AppPage loaded_page_ = model::AppPage::Receive;
    bool has_loaded_page_ = false;
    bool page_switch_scheduled_ = false;
};

} // namespace app
