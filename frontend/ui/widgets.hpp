#pragma once

#include "theme.hpp"

#include <nfd.h>
#include <string>
#include <functional>

namespace prospero::gui {

enum class ToolIcon {
    Home,
    Inspect,
    Build,
    Pfsc,
    Keys,
};

enum class NoticeKind {
    Info,
    Success,
    Warning,
    Error,
};

enum class FilePickerMode {
    OpenFile,
    OpenFolder,
    SaveFile,
};

void draw_background(ImVec2 pos, ImVec2 size);
void text_muted(const char* text);
void text_dim(const char* text);
void draw_separator_text(const char* text);
void section_label(const char* title);
void page_header(const char* title, const char* subtitle, const char* meta = nullptr);

void begin_panel(const char* id, const char* title, ImVec2 size);
void end_panel();

bool styled_button(const char* label, ImVec2 size, ImVec4 base, ImVec4 hover, ImVec4 active);
bool primary_button(const char* label, ImVec2 size);
bool soft_button(const char* label, ImVec2 size);
bool warning_button(const char* label, ImVec2 size);
bool danger_button(const char* label, ImVec2 size);
ImVec2 full_button(float height);

void draw_tool_mark(ToolIcon icon, ImVec2 size, ImVec4 accent);
bool nav_button(const char* id, ToolIcon icon, const char* label, const char* meta, bool selected);
bool command_tile(const char* id, ToolIcon icon, const char* title, const char* meta, ImVec4 accent, bool enabled = true);
void status_pill(const char* label, ImVec4 color);
void notice(const char* id, NoticeKind kind, const char* message);
void kv_row(const char* label, const char* value);
void kv_row(const char* label, const std::string& value);
void kv_row_colored(const char* label, const char* value, ImVec4 value_color);

bool file_path_row(const char* id, const char* label, const char* icon, char* buffer, size_t buffer_size,
                   const char* browse_label, FilePickerMode mode,
                   const nfdfilteritem_t* filters = nullptr, int filter_count = 0);

}
