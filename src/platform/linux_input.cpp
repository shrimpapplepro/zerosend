#include "linux_input.h"

#include "screenshot_service.h"


#include <cstdint>
#include <cstring>

#if !USE_DESKTOP
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <string>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#ifndef APP_KEY_INPUT_DEVICE
#define APP_KEY_INPUT_DEVICE ""
#endif

namespace platform {
namespace {

ScreenKeyHandler screen_key_handler = nullptr;
void* screen_key_user_data = nullptr;
ScreenKeyHandler modal_key_handler = nullptr;
void* modal_key_user_data = nullptr;

#if !USE_DESKTOP
// Device-side Shift state. The desktop virtual keypad does its own modifier
// decoding, so this is only meaningful for the evdev path.
bool shift_held = false;
#endif
uint32_t last_key = 0;
bool last_key_pressed = false;
bool text_input_mode = false;

#if !USE_DESKTOP
struct EvdevKeypad {
    int fd{-1};
    lv_indev_state_t state{LV_INDEV_STATE_RELEASED};
    uint32_t key{0};
    bool router_event_pending{false};
    uint32_t router_key{0};
    bool router_pressed{false};
};
#endif

// CP0 puts the arrows behind fn (fn+F/Z/X/C). The key spec recommends letting
// those four letters steer directly whenever the app is not taking text, which
// here is every screen but the two composers. Applied before the screen hook,
// so screens see LV_KEY_UP and never have to know about the letters.
uint32_t apply_arrow_aliases(uint32_t key) {
    if (text_input_mode) {
        return key;
    }

    switch (key) {
        case 'f':
        case 'F':
            return LV_KEY_UP;
        case 'x':
        case 'X':
            return LV_KEY_DOWN;
        case 'z':
        case 'Z':
            return LV_KEY_LEFT;
        case 'c':
        case 'C':
            return LV_KEY_RIGHT;
        default:
            return key;
    }
}

void dispatch_key(uint32_t raw_key) {
    const auto key = apply_arrow_aliases(raw_key);

    // fn+J is the one app-wide key: it has to work on every screen, including
    // the composers that swallow letters, so it is handled here and never
    // offered to the screen hook.
    if (key == kKeyPrintScreen) {
        const auto result = screenshot::capture_active_screen();
        if (result.success) {
            LV_LOG_USER("screenshot saved: %s", result.path.c_str());
        }
        else {
            LV_LOG_WARN("screenshot failed: %s", result.error.c_str());
        }
        return;
    }

    // The active screen is the only consumer: it binds 'r' to refresh, feeds
    // characters to a composer, or ignores the key entirely.
    if (modal_key_handler && modal_key_handler(key, modal_key_user_data)) {
        return;
    }
    if (screen_key_handler) {
        screen_key_handler(key, screen_key_user_data);
    }
}

void key_event_cb(lv_event_t* event) {
    LV_UNUSED(event);

    auto* indev = lv_indev_active();
    if (!indev) {
        return;
    }

#if !USE_DESKTOP
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (keypad) {
        // LVGL emits LV_EVENT_KEY on every poll, even when evdev had no new event.
        if (!keypad->router_event_pending) {
            return;
        }

        keypad->router_event_pending = false;
        const auto key = keypad->router_key;
        const bool pressed = keypad->router_pressed;

        if (pressed && (!last_key_pressed || last_key != key)) {
            dispatch_key(key);
        }

        last_key = key;
        last_key_pressed = pressed;
        return;
    }
#endif

    const auto key = lv_indev_get_key(indev);
    const bool pressed = lv_indev_get_state(indev) == LV_INDEV_STATE_PRESSED;

    if (pressed && (!last_key_pressed || last_key != key)) {
        dispatch_key(key);
    }

    last_key = key;
    last_key_pressed = pressed;
}

#if !USE_DESKTOP
uint32_t map_evdev_key(uint16_t code) {
    switch (code) {
        case KEY_ESC:
            return LV_KEY_ESC;
        case KEY_LEFT:
            return LV_KEY_LEFT;
        case KEY_RIGHT:
            return LV_KEY_RIGHT;
        case KEY_Z:
            return 'z';
        case KEY_C:
            return 'c';
        case KEY_4:
            return '4';
        case KEY_5:
            return '5';
        case KEY_6:
            return '6';
        case KEY_7:
            return '7';
        case KEY_8:
            return '8';

        // fn+J. The CP0 keyboard reports it as PrintScreen; it never reaches a
        // screen handler, dispatch_key() takes it.
        case KEY_SYSRQ:
            return kKeyPrintScreen;

        // Beyond the template's nav set: editing and list navigation, so the
        // tools are usable on device and not only in the simulator.
        case KEY_UP:
            return LV_KEY_UP;
        case KEY_DOWN:
            return LV_KEY_DOWN;
        case KEY_ENTER:
        case KEY_KPENTER:
            return LV_KEY_ENTER;
        case KEY_TAB:
            return LV_KEY_NEXT;
        case KEY_BACKSPACE:
            return LV_KEY_BACKSPACE;
        case KEY_DELETE:
            return LV_KEY_DEL;
        case KEY_SPACE:
            return ' ';

        default:
            break;
    }

    // Unshifted letters and digits. Shift/symbol layers are not decoded here --
    // that needs modifier tracking against the CP0's real keymap, which cannot
    // be verified without the hardware. The simulator's virtual keypad already
    // decodes the full layout, so text entry is testable there meanwhile.
    if (code >= KEY_1 && code <= KEY_9) {
        return static_cast<uint32_t>('1' + (code - KEY_1));
    }
    if (code == KEY_0) {
        return '0';
    }

    // Punctuation, for typing messages. The M5 driver selects its Sym layer
    // itself and reports the resulting keycode, so these arrive as ordinary
    // keys; Shift is applied below with the US layout. Unverified on hardware.
    switch (code) {
        case KEY_DOT: return '.';
        case KEY_COMMA: return ',';
        case KEY_MINUS: return '-';
        case KEY_EQUAL: return '=';
        case KEY_SLASH: return '/';
        case KEY_SEMICOLON: return ';';
        case KEY_APOSTROPHE: return '\'';
        case KEY_LEFTBRACE: return '[';
        case KEY_RIGHTBRACE: return ']';
        case KEY_BACKSLASH: return '\\';
        case KEY_GRAVE: return '`';
        default: break;
    }

    switch (code) {
        case KEY_A: return 'a';
        case KEY_B: return 'b';
        case KEY_D: return 'd';
        case KEY_E: return 'e';
        case KEY_F: return 'f';
        case KEY_G: return 'g';
        case KEY_H: return 'h';
        case KEY_I: return 'i';
        case KEY_J: return 'j';
        case KEY_K: return 'k';
        case KEY_L: return 'l';
        case KEY_M: return 'm';
        case KEY_N: return 'n';
        case KEY_O: return 'o';
        case KEY_P: return 'p';
        case KEY_Q: return 'q';
        case KEY_R: return 'r';
        case KEY_S: return 's';
        case KEY_T: return 't';
        case KEY_U: return 'u';
        case KEY_V: return 'v';
        case KEY_W: return 'w';
        case KEY_X: return 'x';
        case KEY_Y: return 'y';
        default:
            return 0;
    }
}

bool has_nav_keys(int fd) {
    unsigned long key_bits[(KEY_MAX / (sizeof(unsigned long) * 8)) + 1] = {};
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits) < 0) {
        return false;
    }

    auto has_key = [&](int code) {
        const auto bits_per_word = static_cast<int>(sizeof(unsigned long) * 8);
        return (key_bits[code / bits_per_word] & (1UL << (code % bits_per_word))) != 0;
    };

    return has_key(KEY_ESC) || has_key(KEY_LEFT) || has_key(KEY_RIGHT) || has_key(KEY_Z) ||
           has_key(KEY_C) || has_key(KEY_4) || has_key(KEY_5) || has_key(KEY_6) ||
           has_key(KEY_7) || has_key(KEY_8) || has_key(KEY_SYSRQ);
}

void evdev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    data->continue_reading = false;
    if (!keypad) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    input_event input{};
    while (read(keypad->fd, &input, sizeof(input)) == sizeof(input)) {
        if (input.type != EV_KEY) {
            continue;
        }

        // Shift reaches userspace as an ordinary keycode -- the M5 driver's Fn
        // and Sym layers are selected inside the driver, but Shift is not. So
        // capitals are ours to apply. Tracked here and swallowed, never
        // dispatched as a key of its own.
        if (input.code == KEY_LEFTSHIFT || input.code == KEY_RIGHTSHIFT) {
            if (input.value != 2) {
                shift_held = input.value != 0;
            }
            continue;
        }

        auto key = map_evdev_key(input.code);
        if (!key || input.value == 2) {
            continue;
        }

        if (shift_held && key >= 'a' && key <= 'z') {
            key = key - 'a' + 'A';
        }
        else if (shift_held && key < 128) {
            static constexpr const char* kPlain = "1234567890.,-=/;'[]\\`";
            static constexpr const char* kShifted = "!@#$%^&*()><_+?:\"{}|~";
            if (const char* at = std::strchr(kPlain, static_cast<int>(key)); at && key) {
                key = static_cast<uint32_t>(kShifted[at - kPlain]);
            }
        }

        keypad->key = key;
        keypad->state = input.value ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        keypad->router_event_pending = true;
        keypad->router_key = key;
        keypad->router_pressed = keypad->state == LV_INDEV_STATE_PRESSED;
        data->continue_reading = true;
        break;
    }

    data->key = keypad->key;
    data->state = keypad->state;
}

void evdev_delete_cb(lv_event_t* event) {
    auto* indev = static_cast<lv_indev_t*>(lv_event_get_target(event));
    auto* keypad = static_cast<EvdevKeypad*>(lv_indev_get_driver_data(indev));
    if (!keypad) {
        return;
    }

    if (keypad->fd >= 0) {
        close(keypad->fd);
    }
    delete keypad;
}

lv_indev_t* create_keypad_from_fd(int fd) {
    auto* keypad = new EvdevKeypad;
    keypad->fd = fd;

    auto* indev = lv_indev_create();
    if (!indev) {
        delete keypad;
        close(fd);
        return nullptr;
    }

    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, evdev_read_cb);
    lv_indev_set_driver_data(indev, keypad);
    lv_indev_add_event_cb(indev, evdev_delete_cb, LV_EVENT_DELETE, nullptr);
    attach_key_router(indev);
    return indev;
}

lv_indev_t* try_create_keypad(const char* path) {
    if (!path || path[0] == '\0') {
        return nullptr;
    }

    const int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        LV_LOG_WARN("failed to open input device %s: %s", path, strerror(errno));
        return nullptr;
    }

    if (!has_nav_keys(fd)) {
        close(fd);
        return nullptr;
    }

    LV_LOG_INFO("using evdev key input %s", path);
    return create_keypad_from_fd(fd);
}

void discover_keypads(lv_display_t* display) {
    const char* configured_device = APP_KEY_INPUT_DEVICE;
    if (configured_device[0] != '\0') {
        auto* indev = try_create_keypad(configured_device);
        if (indev) {
            lv_indev_set_display(indev, display);
        }
        return;
    }

    auto* dir = opendir("/dev/input");
    if (!dir) {
        LV_LOG_WARN("failed to open /dev/input: %s", strerror(errno));
        return;
    }

    while (auto* entry = readdir(dir)) {
        if (std::strncmp(entry->d_name, "event", 5) != 0) {
            continue;
        }

        std::string path = "/dev/input/";
        path += entry->d_name;
        auto* indev = try_create_keypad(path.c_str());
        if (indev) {
            lv_indev_set_display(indev, display);
        }
    }

    closedir(dir);
}
#endif

} // namespace

void init_key_input(lv_display_t* display) {
#if !USE_DESKTOP
    discover_keypads(display);
#else
    LV_UNUSED(display);
#endif
}

void attach_key_router(lv_indev_t* indev) {
    if (!indev || lv_indev_get_type(indev) != LV_INDEV_TYPE_KEYPAD) {
        return;
    }

    lv_indev_add_event_cb(indev, key_event_cb, LV_EVENT_KEY, nullptr);
}

void set_text_input_mode(bool enabled) {
    text_input_mode = enabled;
}

void set_screen_key_handler(ScreenKeyHandler handler, void* user_data) {
    screen_key_handler = handler;
    screen_key_user_data = user_data;

    // Every screen starts out steering with F/Z/X/C. A composer opts back into
    // text after installing, so a screen left behind cannot strand the arrows.
    text_input_mode = false;
}

void clear_screen_key_handler(void* user_data) {
    // Only the owner may clear. The incoming screen is built before the
    // outgoing one is destroyed, so an unguarded clear would drop the new
    // screen's handler and leave the app deaf to everything but nav keys.
    if (screen_key_user_data != user_data) {
        return;
    }

    screen_key_handler = nullptr;
    screen_key_user_data = nullptr;
}

} // namespace platform

namespace platform {

void set_modal_key_handler(ScreenKeyHandler handler, void* user_data) {
    modal_key_handler = handler;
    modal_key_user_data = user_data;
}

} // namespace platform
