/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

#include "application_config.h"
#include "asset_manager.h"
#include "logger.h"
#include "linux_input.h"
#include "screen_manager.h"
#include "base_viewmodel.h"
#include "theme.h"
#include "transfer_overlay.h"
#include "zerosend_service.h"

#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if USE_DESKTOP
#include "desktop_launcher.h"
#include "desktop_simulator_frame.h"
#endif

#if !USE_DESKTOP
#if APP_USE_DRM
#include "src/drivers/display/drm/lv_linux_drm.h"
#else
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#endif
#include <unistd.h>
#endif

#ifndef APP_FRAMEBUFFER_DEVICE
#define APP_FRAMEBUFFER_DEVICE "/dev/fb0"
#endif

#ifndef APP_DRM_DEVICE
#define APP_DRM_DEVICE "/dev/dri/card0"
#endif

#ifndef APP_DRM_CONNECTOR_ID
#define APP_DRM_CONNECTOR_ID -1
#endif

#ifndef APP_CONFIG_FILE
#define APP_CONFIG_FILE "zerosend.conf"
#endif

#ifndef APP_DAEMON_PATH
#define APP_DAEMON_PATH "/usr/lib/zerosend/zerosend-daemon"
#endif

namespace app {
namespace {

void quit_requested_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* running = static_cast<bool*>(lv_observer_get_user_data(observer));
    if (running && lv_subject_get_int(subject)) {
        *running = false;
    }
}

struct DarkModePersistence {
    std::string config_path;
    bool last_dark_mode;
};

// Where the daemon's settings and log live: ~/.config/zerosend, next to the
// daemon's own identity.pem and paired-v2.json.
std::filesystem::path state_directory() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0] == '/') {
        return std::filesystem::path(xdg) / "zerosend";
    }
    const char* home = std::getenv("HOME");
    return std::filesystem::path(home ? home : "/tmp") / ".config" / "zerosend";
}

// The LocalSend daemon: ZEROSEND_DAEMON wins (for trying a new build), else
// the path CMake baked in -- the cargo output on desktop, the package's copy
// on the device.
net::ServiceOptions service_options() {
    net::ServiceOptions options;
    const char* override_path = std::getenv("ZEROSEND_DAEMON");
    options.daemon_path = override_path && override_path[0] ? override_path : APP_DAEMON_PATH;
    const auto dir = state_directory();
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    options.log_path = (dir / "daemon.log").string();
    net::load_service_settings((dir / "settings.conf").string(), options);
    return options;
}

std::string writable_config_path() {
#if USE_DESKTOP
    return APP_CONFIG_FILE;
#else
    if (const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME")) {
        const std::filesystem::path root(xdg_config_home);
        if (!root.empty() && root.is_absolute()) {
            return (root / "zerosend" / "zerosend.conf").string();
        }
    }
    if (const char* home = std::getenv("HOME")) {
        const std::filesystem::path root(home);
        if (!root.empty() && root.is_absolute()) {
            return (root / ".config" / "zerosend" / "zerosend.conf").string();
        }
    }
    return APP_CONFIG_FILE;
#endif
}

void persist_dark_mode_observer(lv_observer_t* observer, lv_subject_t* subject) {
    auto* persistence = static_cast<DarkModePersistence*>(lv_observer_get_user_data(observer));
    if (!persistence) {
        return;
    }
    const bool dark_mode = lv_subject_get_int(subject) != 0;
    if (dark_mode == persistence->last_dark_mode) {
        return;
    }
    persistence->last_dark_mode = dark_mode;

    ApplicationConfig config;
    config.dark_mode = dark_mode;
    std::string error;
    if (save_application_config(persistence->config_path, config, error)) {
        LOG_INFO("saved config: {} (dark_mode={})",
                 persistence->config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to save config {}: {}", persistence->config_path, error);
    }
}

#if !USE_DESKTOP
#if !APP_USE_DRM
// The CP0's ST7789V LCD is not reliably /dev/fb0 -- that node is often HDMI or
// another framebuffer, and the panel commonly lands on /dev/fb1. The upstream
// template hardcodes APP_FRAMEBUFFER_DEVICE and returns a null display when it
// guesses wrong, which looks like a dead app rather than a misconfiguration.
// Resolve it the way M5's own apps do: explicit env override, then /proc/fb,
// then the compiled-in default as a last resort.
std::string resolve_framebuffer_device() {
    if (const char* forced = std::getenv("LV_LINUX_FBDEV_DEVICE"); forced && *forced) {
        LOG_INFO("framebuffer forced by LV_LINUX_FBDEV_DEVICE: {}", forced);
        return forced;
    }

    // /proc/fb lines are "<index> <driver-name>", e.g. "1 fb_st7789v".
    std::ifstream proc_fb("/proc/fb");
    if (proc_fb) {
        std::string line;
        while (std::getline(proc_fb, line)) {
            std::istringstream fields(line);
            int index = -1;
            std::string driver;
            if (!(fields >> index >> driver)) {
                continue;
            }
            if (driver.find("st7789v") != std::string::npos) {
                std::string path = "/dev/fb" + std::to_string(index);
                LOG_INFO("framebuffer detected from /proc/fb: {} ({})", path, driver);
                return path;
            }
        }
        LOG_WARN("no st7789v entry in /proc/fb; falling back to {}", APP_FRAMEBUFFER_DEVICE);
    } else {
        LOG_WARN("cannot read /proc/fb; falling back to {}", APP_FRAMEBUFFER_DEVICE);
    }

    return APP_FRAMEBUFFER_DEVICE;
}
#endif

// The CP0 LCD is a DRM SPI panel; /dev/fb0 is the kernel's fbdev emulation,
// which sends mmap writes to the panel from a deferred-I/O timer (every 50 ms),
// capping what reaches the glass at ~10-20 fps and adding up to 50 ms to every
// key press. fsync() on the fbdev runs that flush at once, so a frame that
// changed a large part of the screen goes out immediately; small updates (the
// clock) keep the batched pace. ZEROSEND_FB_SYNC=0 turns it off.
int g_frame_push_fd = -1;
uint32_t g_frame_pixels = 0;
uint32_t g_frame_push_min_pixels = 0;

void frame_push_cb(lv_event_t* event) {
    if (lv_event_get_code(event) == LV_EVENT_FLUSH_START) {
        if (const auto* area = static_cast<const lv_area_t*>(lv_event_get_param(event))) {
            g_frame_pixels += static_cast<uint32_t>(lv_area_get_size(area));
        }
        return;
    }
    if (g_frame_pixels > 0 && g_frame_pixels >= g_frame_push_min_pixels) {
        fsync(g_frame_push_fd);
    }
    g_frame_pixels = 0;
}

[[maybe_unused]] void enable_frame_push(lv_display_t* display, const std::string& framebuffer) {
    const char* enabled = std::getenv("ZEROSEND_FB_SYNC");
    if (enabled && std::strcmp(enabled, "0") == 0) {
        return;
    }
    g_frame_push_fd = open(framebuffer.c_str(), O_RDWR | O_CLOEXEC);
    if (g_frame_push_fd < 0) {
        return;
    }
    const uint32_t screen = static_cast<uint32_t>(lv_display_get_horizontal_resolution(display)) *
                            static_cast<uint32_t>(lv_display_get_vertical_resolution(display));
    g_frame_push_min_pixels = screen * 12U / 100U;  // a page switch, not the clock
    lv_display_add_event_cb(display, frame_push_cb, LV_EVENT_FLUSH_START, nullptr);
    lv_display_add_event_cb(display, frame_push_cb, LV_EVENT_REFR_READY, nullptr);
}

lv_display_t* init_device_display() {
#if APP_USE_DRM
    auto* display = lv_linux_drm_create();
    if (!display) {
        return nullptr;
    }

    if (lv_linux_drm_set_file(display, APP_DRM_DEVICE, APP_DRM_CONNECTOR_ID) != LV_RESULT_OK) {
        lv_display_delete(display);
        return nullptr;
    }

    platform::init_key_input(display);
    return display;
#else
    auto* display = lv_linux_fbdev_create();
    if (!display) {
        return nullptr;
    }

    const std::string framebuffer = resolve_framebuffer_device();
    if (lv_linux_fbdev_set_file(display, framebuffer.c_str()) != LV_RESULT_OK) {
        LOG_ERROR("cannot open framebuffer {}", framebuffer);
        lv_display_delete(display);
        return nullptr;
    }

    // No lv_linux_fbdev_set_force_refresh(). On the CP0's DRM-backed fbdev
    // (panel-mipi-dbi) each FBIOPUT_VSCREENINFO+FORCE is a forced mode-set and a
    // full SPI push; per flushed strip it made redraws slow and tear. Measured in
    // Perlica's Tools, 2026-11-19; CardBili does the same and reaches 30 fps.

    enable_frame_push(display, framebuffer);
    platform::init_key_input(display);
    return display;
#endif
}
#endif

} // namespace

int Application::run() {
    logger::Logger::init();
    logger::Logger::set_tag("zerosend");

    lv_init();

    AssetManager assets;
    for (const auto& root : assets.roots()) {
        LOG_INFO("asset root: {}", root.string());
    }

    viewmodel::BaseViewModel view_model;
    const std::string user_config_path = writable_config_path();
    std::string loaded_config_path = APP_CONFIG_FILE;
    if (user_config_path != APP_CONFIG_FILE) {
        std::error_code filesystem_error;
        if (std::filesystem::is_regular_file(user_config_path, filesystem_error)) {
            loaded_config_path = user_config_path;
        }
    }

    ApplicationConfig config;
    std::string config_error;
    if (load_application_config(loaded_config_path, config, config_error)) {
        view_model.set_dark_mode(config.dark_mode);
        LOG_INFO("loaded config: {} (dark_mode={})",
                 loaded_config_path,
                 config.dark_mode ? "yes" : "no");
    } else {
        LOG_WARN("failed to load config {}: {}; using defaults", loaded_config_path, config_error);
    }

#if USE_DESKTOP
    DesktopSimulatorFrame simulator_frame(assets);
    auto* display = simulator_frame.display();
#else
    auto* display = init_device_display();
    // The fbdev/DRM drivers register a tick source but no delay. Without one,
    // lv_delay_ms() in the main loop spin-waits, pinning a whole core (99% CPU
    // measured) -- on the CP0 that is battery. The simulator sets
    // SDL_Delay for the same reason.
    lv_delay_set_cb([](uint32_t ms) { usleep(ms * 1000U); });
#endif
    if (!display) {
        LOG_ERROR("failed to initialize display");
        return 1;
    }

    view::apply_lvgl_theme(display, view_model.is_dark_mode());

#if USE_DESKTOP
    simulator_frame.bind_dark_mode(view_model.dark_mode_subject());
#endif

    ScreenManager screen_manager(view_model, assets);

#if USE_DESKTOP
    // Boot into the launcher stand-in, the way the device does, so the icon
    // and the open/return flow are visible in the simulator. It starts the
    // screen manager when something is launched -- until then nothing may
    // load a screen over the launcher.
    DesktopLauncher launcher(assets, view_model, screen_manager);
    simulator_frame.set_home_handler([&launcher]() { launcher.show(); });
    launcher.show();
#else
    screen_manager.start();
#endif

    // LocalSend: start the daemon, and float the transfer card over every page.
    auto& service = net::Service::instance();
    service.set_settings_path((state_directory() / "settings.conf").string());
    service.start(service_options());
    view::widgets::TransferOverlay transfer_overlay(view_model, assets);

    DarkModePersistence dark_mode_persistence{user_config_path, view_model.is_dark_mode()};
    auto* dark_mode_observer = lv_subject_add_observer(view_model.dark_mode_subject(),
                                                        persist_dark_mode_observer,
                                                        &dark_mode_persistence);

    bool running = true;
    auto* quit_observer = lv_subject_add_observer(view_model.quit_requested_subject(),
                                                  quit_requested_observer,
                                                  &running);

    LOG_INFO("LVGL app started at {}x{}", lv_display_get_horizontal_resolution(display),
             lv_display_get_vertical_resolution(display));
    while (running
#if USE_DESKTOP
           && simulator_frame.process_events()
#endif
    ) {
        // Sleep until LVGL's next timer is due (refresh 16 ms, input read 30 ms) instead of
        // waking every 5 ms: ~60 wakeups/s instead of ~200 on a battery device.
        const uint32_t next = lv_timer_handler();
#if USE_DESKTOP
        lv_delay_ms(next == LV_NO_TIMER_READY ? 16 : LV_CLAMP(1, next, 16));
#else
        // Sleep until a key, a worker's wake_main_loop(), or LVGL's next timer.
        // LVGL pauses its refresh timer while nothing is invalid, so an idle
        // screen wakes only for its own timers instead of ~60 times a second.
        platform::wait_for_key_input(next == LV_NO_TIMER_READY ? -1 : static_cast<int>(next));
#endif
    }

    if (quit_observer) {
        lv_observer_remove(quit_observer);
    }
    service.stop();
    if (dark_mode_observer) {
        lv_observer_remove(dark_mode_observer);
    }


    return 0;
}

} // namespace app
