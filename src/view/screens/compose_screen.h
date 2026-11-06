/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_screen.h"

#include <string>

namespace screen {

// Type a text. Opened two ways, like LocalSend's "Text" button:
//  - from the Send tab (T): Enter puts the text in the selection;
//  - on a device with nothing selected: Enter sends it to that device now.
class ComposeScreen : public ToolScreen {
public:
    ComposeScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~ComposeScreen() override;

private:
    void build_content(lv_obj_t* content) override;
    bool on_key(uint32_t key) override;
    void render();

    std::string input_;
    lv_obj_t* to_{nullptr};
    lv_obj_t* box_{nullptr};
    lv_obj_t* text_{nullptr};
    lv_obj_t* counter_{nullptr};
};

} // namespace screen
