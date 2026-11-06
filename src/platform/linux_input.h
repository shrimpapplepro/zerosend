#pragma once

#include "lvgl.h"

#include <cstdint>

namespace platform {

// Synthetic keycodes for keys with no LVGL equivalent, kept clear of both ASCII
// and LVGL's own LV_KEY_* range. Same base and offsets as the upstream Template,
// so code ported either way keeps working.
constexpr uint32_t kSpecialKeyBase = 0x200000U;
constexpr uint32_t kKeyPrintScreen = kSpecialKeyBase | 18U;

void init_key_input(lv_display_t* display);
void attach_key_router(lv_indev_t* indev);

// Text entry claims the letter keys. While this is on, F/Z/X/C stay letters;
// while it is off they steer as arrows (see the CP0 key spec). Installing a
// screen key handler turns it off, so a screen that takes text must switch it
// on after install_key_handler() and back off when it stops taking text.
void set_text_input_mode(bool enabled);

// Screen-level key hook. Every key the input layer decodes arrives here, and
// the active screen is the only thing that acts on it -- the bottom bar names
// keys, it does not own them. Returning true consumes the key.
//
// Registration is keyed by user_data: a screen clears only its own handler.
// That matters because ScreenManager builds the incoming screen before
// destroying the outgoing one, so an unguarded clear in a destructor would wipe
// the handler the new screen just installed.
using ScreenKeyHandler = bool (*)(uint32_t key, void* user_data);

void set_screen_key_handler(ScreenKeyHandler handler, void* user_data);
void clear_screen_key_handler(void* user_data);

// App-wide modal hook, asked before the screen handler. The transfer overlay
// uses it: an incoming request has to be answerable on every page, and while
// it is up the page underneath must not see the keys. Returning false lets the
// key through to the screen.
void set_modal_key_handler(ScreenKeyHandler handler, void* user_data);

} // namespace platform
