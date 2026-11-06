/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_widget.h"
#include "base_viewmodel.h"

#include <array>

namespace app {
class AssetManager;
}

namespace view::widgets {

// The compact bottom bar from the CP0 key spec: the keys this page answers to,
// named with what they do. No buttons -- every action here has a physical key,
// and the CP0 has no pointer to press an on-screen one with.
class NavBar : public BaseWidgets {
public:
    NavBar(lv_obj_t* parent, viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~NavBar() override;

    void build() override;

private:
    static constexpr std::size_t kHintCount = 4;

    void create_shortcut_hints();
    void update_hints();

    static void update_hints_cb(lv_observer_t* observer, lv_subject_t* subject);

    viewmodel::BaseViewModel& view_model_;
    app::AssetManager& assets_;
    std::array<lv_obj_t*, kHintCount> hint_key_labels_{};
    std::array<lv_obj_t*, kHintCount> hint_text_labels_{};
    lv_obj_t* shortcut_bar_{nullptr};
    lv_observer_t* page_observer_{nullptr};
    lv_observer_t* theme_observer_{nullptr};
};

} // namespace view::widgets
