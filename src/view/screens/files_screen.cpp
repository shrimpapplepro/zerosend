/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "files_screen.h"

#include "asset_manager.h"
#include "format.h"
#include "theme.h"

#include <algorithm>
#include <cstdlib>
#include <system_error>

namespace screen {
namespace {

namespace fs = std::filesystem;

constexpr int kRowHeight = 18;
// A folder with more entries than this lists only the first ones; the rest
// are still sent when the folder itself is marked.
constexpr std::size_t kMaxEntries = 400;
// Selection sizes walk marked folders; this bounds the walk on a huge tree.
constexpr std::size_t kMaxSizeWalk = 20000;

// Remembered across visits, so a second file from the same folder is quick.
fs::path& last_directory() {
    static fs::path directory;
    return directory;
}

const char* file_icon(const fs::path& path) {
    auto ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    if (ext == ".jpg" || ext == ".jpeg" || ext == ".png" || ext == ".gif" || ext == ".webp" || ext == ".heic") {
        return view::ICON_IMAGE;
    }
    if (ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".m4a" || ext == ".ogg") {
        return view::ICON_MUSIC;
    }
    if (ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".webm") {
        return view::ICON_VIDEO;
    }
    if (ext == ".txt" || ext == ".md" || ext == ".log" || ext == ".conf" || ext == ".json") {
        return view::ICON_FILE_TEXT;
    }
    return view::ICON_FILE;
}

uint64_t size_of(const fs::path& path, std::size_t& budget) {
    std::error_code error;
    if (fs::is_regular_file(path, error)) {
        return fs::file_size(path, error);
    }
    uint64_t total = 0;
    for (auto it = fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::recursive_directory_iterator() && budget > 0;
         it.increment(error)) {
        --budget;
        if (it->is_regular_file(error)) {
            total += it->file_size(error);
        }
    }
    return total;
}

} // namespace

FilesScreen::FilesScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets)
    : ToolScreen(view_model, assets) {
    init();
}

void FilesScreen::build_content(lv_obj_t* content) {
    auto* body = assets().load_font("inter-regular.ttf", 12);
    auto* small = assets().load_font("inter-regular.ttf", 10);
    auto* icon = assets().load_font("Phosphor-Fill.ttf", 13);
    font_body_ = body ? body : &lv_font_montserrat_12;
    font_small_ = small ? small : &lv_font_montserrat_12;
    font_icon_ = icon ? icon : &lv_font_montserrat_12;

    path_ = lv_label_create(content);
    lv_obj_set_style_text_font(path_, font_small_, 0);
    lv_label_set_long_mode(path_, LV_LABEL_LONG_DOT);
    lv_obj_set_width(path_, 230);
    lv_obj_align(path_, LV_ALIGN_TOP_LEFT, 8, 2);

    marks_ = lv_label_create(content);
    lv_obj_set_style_text_font(marks_, font_small_, 0);
    lv_obj_align(marks_, LV_ALIGN_TOP_RIGHT, -8, 2);

    list_ = lv_obj_create(content);
    lv_obj_remove_style_all(list_);
    lv_obj_set_size(list_, LV_PCT(100), 120 - 16);
    lv_obj_align(list_, LV_ALIGN_TOP_LEFT, 0, 16);
    lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_hor(list_, 4, 0);
    lv_obj_set_scroll_dir(list_, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_, LV_SCROLLBAR_MODE_OFF);

    const char* home = std::getenv("HOME");
    root_ = home ? fs::path(home) : fs::path("/");
    std::error_code error;
    const auto start = !last_directory().empty() && fs::is_directory(last_directory(), error) ? last_directory()
                                                                                              : root_;
    install_key_handler();
    open(start);
}

void FilesScreen::open(const fs::path& directory, const std::string& focus) {
    std::error_code error;
    std::vector<Entry> entries;
    error_.clear();
    for (auto it = fs::directory_iterator(directory, fs::directory_options::skip_permission_denied, error);
         !error && it != fs::directory_iterator();
         it.increment(error)) {
        const auto name = it->path().filename().string();
        if (name.empty() || name[0] == '.') {
            continue; // hidden, as in LocalSend's own picker
        }
        Entry entry;
        entry.name = name;
        entry.path = it->path();
        std::error_code entry_error;
        entry.directory = it->is_directory(entry_error);
        if (!entry.directory) {
            if (!it->is_regular_file(entry_error)) {
                continue;
            }
            entry.size = it->file_size(entry_error);
        }
        entries.push_back(std::move(entry));
        if (entries.size() >= kMaxEntries) {
            break;
        }
    }
    if (error) {
        error_ = error.message();
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        if (a.directory != b.directory) {
            return a.directory;
        }
        return a.name < b.name;
    });

    cwd_ = directory;
    last_directory() = directory;
    entries_ = std::move(entries);
    cursor_ = 0;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (entries_[i].name == focus) {
            cursor_ = static_cast<int>(i);
        }
    }
    render();
}

void FilesScreen::render() {
    const auto colors = view::palette(view_model().is_dark_mode());

    std::string shown = cwd_.string();
    if (const char* home = std::getenv("HOME"); home && shown.rfind(home, 0) == 0) {
        shown = "~" + shown.substr(std::char_traits<char>::length(home));
    }
    lv_label_set_text(path_, shown.empty() ? "/" : shown.c_str());
    lv_obj_set_style_text_color(path_, colors.text, 0);
    lv_obj_set_style_text_opa(path_, LV_OPA_70, 0);

    if (marked_.empty()) {
        lv_label_set_text(marks_, "");
    } else {
        lv_label_set_text_fmt(marks_, "%u marked", static_cast<unsigned>(marked_.size()));
    }
    lv_obj_set_style_text_color(marks_, colors.primary, 0);

    lv_obj_clean(list_);
    if (entries_.empty()) {
        auto* empty = lv_label_create(list_);
        lv_label_set_text(empty, error_.empty() ? "Empty folder" : error_.c_str());
        lv_obj_set_style_text_font(empty, font_body_, 0);
        lv_obj_set_style_text_color(empty, error_.empty() ? colors.text : colors.error, 0);
        lv_obj_set_style_text_opa(empty, LV_OPA_60, 0);
        lv_obj_set_style_pad_all(empty, 8, 0);
        return;
    }

    // Only a window of rows around the cursor is built: a folder of hundreds
    // of photos would otherwise cost hundreds of LVGL objects per keypress.
    constexpr int kWindow = 24;
    const int count = static_cast<int>(entries_.size());
    const int first = std::max(0, std::min(cursor_ - kWindow / 2, count - kWindow));
    const int last = std::min(count, first + kWindow);
    for (int index = first; index < last; ++index) {
        const auto& entry = entries_[index];
        const bool active = index == cursor_;
        const bool marked = marked_.count(entry.path.string()) > 0;

        auto* row = lv_obj_create(list_);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), kRowHeight);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_bg_color(row, colors.button, 0);
        lv_obj_set_style_bg_opa(row, active ? LV_OPA_COVER : LV_OPA_TRANSP, 0);

        auto* check = lv_label_create(row);
        lv_label_set_text(check, marked ? view::ICON_CHECK_SQUARE : "");
        lv_obj_set_style_text_font(check, font_icon_, 0);
        lv_obj_set_style_text_color(check, colors.primary, 0);
        lv_obj_align(check, LV_ALIGN_LEFT_MID, 3, 0);

        auto* icon = lv_label_create(row);
        lv_label_set_text(icon, entry.directory ? view::ICON_FOLDER : file_icon(entry.path));
        lv_obj_set_style_text_font(icon, font_icon_, 0);
        lv_obj_set_style_text_color(icon, entry.directory ? colors.warning : colors.text, 0);
        lv_obj_align(icon, LV_ALIGN_LEFT_MID, 19, 0);

        auto* name = lv_label_create(row);
        lv_label_set_text(name, entry.name.c_str());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 210);
        lv_obj_set_style_text_font(name, font_body_, 0);
        lv_obj_set_style_text_color(name, active || marked ? colors.primary : colors.text, 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 37, 0);

        if (!entry.directory) {
            auto* size = lv_label_create(row);
            lv_label_set_text(size, view::format_bytes(entry.size).c_str());
            lv_obj_set_style_text_font(size, font_small_, 0);
            lv_obj_set_style_text_color(size, colors.text, 0);
            lv_obj_set_style_text_opa(size, LV_OPA_60, 0);
            lv_obj_align(size, LV_ALIGN_RIGHT_MID, -4, 0);
        }

        if (active) {
            lv_obj_update_layout(list_);
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }
}

void FilesScreen::select_and_leave(const std::vector<std::string>& paths) {
    std::size_t budget = kMaxSizeWalk;
    uint64_t total = 0;
    for (const auto& path : paths) {
        total += size_of(path, budget);
    }
    view_model().select_files(paths, total);
    view_model().go_home();
}

bool FilesScreen::on_escape() {
    // Up one level until the starting folder, then out.
    if (cwd_ == root_ || !cwd_.has_parent_path() || cwd_ == cwd_.parent_path()) {
        return false;
    }
    const auto child = cwd_.filename().string();
    open(cwd_.parent_path(), child);
    return true;
}

bool FilesScreen::on_key(uint32_t key) {
    const int count = static_cast<int>(entries_.size());
    switch (key) {
        case LV_KEY_UP:
            if (cursor_ > 0) {
                --cursor_;
                render();
            }
            return true;
        case LV_KEY_DOWN:
            if (cursor_ + 1 < count) {
                ++cursor_;
                render();
            }
            return true;
        case LV_KEY_LEFT:
            if (cwd_.has_parent_path() && cwd_ != cwd_.parent_path()) {
                const auto child = cwd_.filename().string();
                open(cwd_.parent_path(), child);
            }
            return true;
        case LV_KEY_RIGHT:
        case LV_KEY_ENTER: {
            if (count == 0) {
                return true;
            }
            const auto& entry = entries_[cursor_];
            if (entry.directory) {
                open(entry.path);
            } else if (key == LV_KEY_ENTER) {
                if (marked_.empty()) {
                    select_and_leave({entry.path.string()});
                } else {
                    const auto path = entry.path.string();
                    if (!marked_.erase(path)) {
                        marked_.insert(path);
                    }
                    render();
                }
            }
            return true;
        }
        case ' ': {
            if (count == 0) {
                return true;
            }
            const auto path = entries_[cursor_].path.string();
            if (!marked_.erase(path)) {
                marked_.insert(path);
            }
            if (cursor_ + 1 < count) {
                ++cursor_;
            }
            render();
            return true;
        }
        case 's':
        case 'S':
            if (!marked_.empty()) {
                select_and_leave({marked_.begin(), marked_.end()});
            } else if (count > 0) {
                select_and_leave({entries_[cursor_].path.string()});
            }
            return true;
        default:
            return false;
    }
}

} // namespace screen
