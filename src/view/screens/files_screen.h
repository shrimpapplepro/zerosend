/*
 * SPDX-FileCopyrightText: 2026 ZeroSend contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "tool_screen.h"

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace screen {

// A file browser that fills the Send tab's selection.
//   Enter  open a folder; on a file, select it (or toggle it while marking)
//   Space  mark the row (folders are sent whole)
//   S      select the marked rows, or the current file
//   ESC    up one folder; out to the Send tab from the starting folder
class FilesScreen : public ToolScreen {
public:
    FilesScreen(viewmodel::BaseViewModel& view_model, app::AssetManager& assets);

private:
    struct Entry {
        std::string name;
        std::filesystem::path path;
        bool directory{false};
        uint64_t size{0};
    };

    void build_content(lv_obj_t* content) override;
    bool on_key(uint32_t key) override;
    bool on_escape() override;

    void open(const std::filesystem::path& directory, const std::string& focus = {});
    void render();
    void select_and_leave(const std::vector<std::string>& paths);

    std::filesystem::path root_;
    std::filesystem::path cwd_;
    std::vector<Entry> entries_;
    std::set<std::string> marked_;
    int cursor_{0};
    std::string error_;

    lv_obj_t* path_{nullptr};
    lv_obj_t* marks_{nullptr};
    lv_obj_t* list_{nullptr};
    const lv_font_t* font_body_{nullptr};
    const lv_font_t* font_small_{nullptr};
    const lv_font_t* font_icon_{nullptr};
};

} // namespace screen
