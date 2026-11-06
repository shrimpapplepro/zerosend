/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

// One of the three LocalSend tabs. Left/Right (Z/C on the CP0) and 1/2/3
// switch tabs; every other key goes to the subclass first, so a tab that is
// editing text can keep the digits for itself by consuming them.
//
// ESC is not bound: on a tab there is nothing to go back to, and holding ESC
// is how the CardputerZero leaves an app.
class TabScreen : public BaseScreen {
public:
    TabScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~TabScreen() override;

protected:
    // Return true to consume the key.
    virtual bool on_key(uint32_t key);

    void install_key_handler();

    const lv_font_t* font(const char* file, uint32_t size, const lv_font_t* fallback);

private:
    static bool key_handler(uint32_t key, void* user_data);
};

} // namespace screen
