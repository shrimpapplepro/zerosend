/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "base_screen.h"

namespace screen {

// Shared behaviour for every tool page: ESC returns to the launcher, and keys
// are routed to the subclass through on_key().
//
// Subclasses must call install_key_handler() once their content exists, and
// must not register a platform key handler of their own -- there is only one
// slot, so a second registration would silently displace this one.
class ToolScreen : public BaseScreen {
public:
    ToolScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);
    ~ToolScreen() override;

protected:
    // Return true to consume the key. ESC is handled by the base before this is
    // called, so a subclass never needs to implement "go back".
    virtual bool on_key(uint32_t key);

    // Called before ESC goes home. Return true to consume it -- for a screen
    // with an overlay open, where ESC should dismiss that first. Overrides must
    // consume ESC only while there is something to dismiss, or the page traps
    // the user with no way back.
    virtual bool on_escape();

    void install_key_handler();

private:
    static bool key_handler(uint32_t key, void* user_data);
};

} // namespace screen
