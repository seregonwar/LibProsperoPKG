#include "widgets.hpp"
#include "ui_icons.hpp"

#include <algorithm>
#include <cstdio>
#include <nfd.h>

namespace prospero::gui {

namespace {

ImVec4 brighten(ImVec4 color, float amount) {
    color.x = std::min(1.0f, color.x * amount);
    color.y = std::min(1.0f, color.y * amount);
    color.z = std::min(1.0f, color.z * amount);
    return color;
}

ImVec4 notice_color(NoticeKind kind) {
    const auto& p = colors();
    switch (kind) {
        case NoticeKind::Success: return p.success;
        case NoticeKind::Warning: return p.amber;
        case NoticeKind::Error: return p.danger;
        case NoticeKind::Info:
        default: return p.primary2;
    }
}

}

void draw_background(ImVec2 pos, ImVec2 size) {
    const auto& p = colors();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    draw_list->AddRectFilled(pos, max, color_u32(p.bg0));
    draw_list->AddRectFilled(pos, ImVec2(max.x, pos.y + 1.0f * dpi_scale()), color_u32(p.border_hot));
}

void text_muted(const char* text) {
    ImGui::TextColored(colors().muted, "%s", text);
}

void text_dim(const char* text) {
    ImGui::TextColored(colors().dim, "%s", text);
}

void draw_separator_text(const char* text) {
    section_label(text);
}

void section_label(const char* title) {
    ImGui::TextColored(colors().muted, "%s", title);
    ImGui::Separator();
}

void page_header(const char* title, const char* subtitle, const char* meta) {
    const auto& p = colors();
    ImGui::SetWindowFontScale(1.2f);
    ImGui::TextColored(p.text, "%s", title);
    ImGui::SetWindowFontScale(1.0f);
    if (meta != nullptr && meta[0] != '\0') {
        ImGui::SameLine();
        status_pill(meta, p.primary2);
    }
    ImGui::TextColored(p.muted, "%s", subtitle);
    ImGui::Spacing();
}

void begin_panel(const char* id, const char* title, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, colors().panel2);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * dpi_scale(), 10.0f * dpi_scale()));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 4.0f * dpi_scale());
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders);
    section_label(title);
}

void end_panel() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

bool styled_button(const char* label, ImVec2 size, ImVec4 base, ImVec4 hover, ImVec4 active) {
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, active);
    const bool pressed = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return pressed;
}

bool primary_button(const char* label, ImVec2 size) {
    return styled_button(label, size,
                         ImVec4(210.0f / 255.0f, 235.0f / 255.0f, 250.0f / 255.0f, 1.0f),
                         ImVec4(180.0f / 255.0f, 220.0f / 255.0f, 245.0f / 255.0f, 1.0f),
                         ImVec4(155.0f / 255.0f, 205.0f / 255.0f, 235.0f / 255.0f, 1.0f));
}

bool soft_button(const char* label, ImVec2 size) {
    const auto& p = colors();
    return styled_button(label, size, p.bg2, p.bg3, ImVec4(190.0f / 255.0f, 205.0f / 255.0f, 218.0f / 255.0f, 1.0f));
}

bool warning_button(const char* label, ImVec2 size) {
    return styled_button(label, size,
                         ImVec4(250.0f / 255.0f, 235.0f / 255.0f, 205.0f / 255.0f, 1.0f),
                         ImVec4(245.0f / 255.0f, 220.0f / 255.0f, 170.0f / 255.0f, 1.0f),
                         ImVec4(235.0f / 255.0f, 200.0f / 255.0f, 140.0f / 255.0f, 1.0f));
}

bool danger_button(const char* label, ImVec2 size) {
    return styled_button(label, size,
                         ImVec4(250.0f / 255.0f, 225.0f / 255.0f, 230.0f / 255.0f, 1.0f),
                         ImVec4(245.0f / 255.0f, 195.0f / 255.0f, 205.0f / 255.0f, 1.0f),
                         ImVec4(235.0f / 255.0f, 170.0f / 255.0f, 185.0f / 255.0f, 1.0f));
}

ImVec2 full_button(float height) {
    return ImVec2(ImGui::GetContentRegionAvail().x, height * dpi_scale());
}

void draw_tool_mark(ToolIcon icon, ImVec2 size, ImVec4 accent) {
    const char* label = "Icon";
    switch (icon) {
        case ToolIcon::Home: label = "H"; break;
        case ToolIcon::Inspect: label = "I"; break;
        case ToolIcon::Build: label = "B"; break;
        case ToolIcon::Pfsc: label = "P"; break;
        case ToolIcon::Keys: label = "K"; break;
    }
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const auto& p = colors();
    const float scl = dpi_scale();
    const float rounding = 4.0f * scl;
    const ImVec2 max(pos.x + size.x, pos.y + size.y);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(pos, max, color_u32(p.bg2), rounding);
    draw_list->AddRect(pos, max, color_u32(with_alpha(accent, 0.8f)), rounding, 0, 1.0f * scl);
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    draw_list->AddText(ImVec2(pos.x + (size.x - text_size.x) * 0.5f,
                              pos.y + (size.y - text_size.y) * 0.5f),
                       color_u32(accent), label);
    ImGui::Dummy(size);
}

bool nav_button(const char* id, ToolIcon icon, const char* label, const char* meta, bool selected) {
    const auto& p = colors();
    const float scl = dpi_scale();
    const float height = 40.0f * scl;
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##nav", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec4 bg = selected ? ImVec4(30.0f / 255.0f, 50.0f / 255.0f, 55.0f / 255.0f, 1.0f)
                               : hovered ? p.bg3 : p.rail;
    const ImVec4 border = selected || hovered ? p.border_hot : with_alpha(p.border, 0.5f);
    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), color_u32(bg), 4.0f * scl);
    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), color_u32(border), 4.0f * scl);
    if (selected) {
        draw_list->AddRectFilled(pos, ImVec2(pos.x + 3.0f * scl, pos.y + height), color_u32(p.primary2), 4.0f * scl);
    }

    const char* icon_label = "?";
    switch (icon) {
        case ToolIcon::Home: icon_label = "H"; break;
        case ToolIcon::Inspect: icon_label = "I"; break;
        case ToolIcon::Build: icon_label = "B"; break;
        case ToolIcon::Pfsc: icon_label = "P"; break;
        case ToolIcon::Keys: icon_label = "K"; break;
    }
    const ImVec2 icon_size = ImGui::CalcTextSize(icon_label);
    draw_list->AddText(ImVec2(pos.x + 12.0f * scl, pos.y + (height - icon_size.y) * 0.5f),
                       color_u32(selected ? p.primary2 : p.primary), icon_label);

    draw_list->AddText(ImVec2(pos.x + 44.0f * scl, pos.y + 8.0f * scl),
                       color_u32(selected ? p.text : p.muted), label);
    if (meta != nullptr && meta[0] != '\0') {
        draw_list->AddText(ImVec2(pos.x + 44.0f * scl, pos.y + 22.0f * scl),
                           color_u32(p.dim), meta);
    }
    ImGui::PopID();
    return clicked;
}

bool command_tile(const char* id, ToolIcon icon, const char* title, const char* meta, ImVec4 accent, bool enabled) {
    const auto& p = colors();
    const float scl = dpi_scale();
    const float height = 70.0f * scl;
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushID(id);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##tile", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked() && enabled;

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec4 bg = hovered && enabled ? p.bg3 : p.panel2;
    ImVec4 border = hovered && enabled ? accent : p.border;
    if (!enabled) {
        bg = with_alpha(bg, 0.6f);
        border = with_alpha(border, 0.4f);
        accent = with_alpha(p.dim, 0.8f);
    }

    draw_list->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), color_u32(bg), 4.0f * scl);
    draw_list->AddRect(pos, ImVec2(pos.x + width, pos.y + height), color_u32(border), 4.0f * scl);
    draw_list->AddRectFilled(pos, ImVec2(pos.x + 3.0f * scl, pos.y + height), color_u32(with_alpha(accent, 0.8f)), 4.0f * scl);

    const char* icon_label = "?";
    switch (icon) {
        case ToolIcon::Home: icon_label = "H"; break;
        case ToolIcon::Inspect: icon_label = "I"; break;
        case ToolIcon::Build: icon_label = "B"; break;
        case ToolIcon::Pfsc: icon_label = "P"; break;
        case ToolIcon::Keys: icon_label = "K"; break;
    }
    const ImVec2 icon_size = ImGui::CalcTextSize(icon_label);
    draw_list->AddText(ImVec2(pos.x + 16.0f * scl, pos.y + (height - icon_size.y) * 0.5f),
                       color_u32(accent), icon_label);

    draw_list->AddText(ImVec2(pos.x + 60.0f * scl, pos.y + 16.0f * scl),
                       color_u32(enabled ? p.text : p.muted), title);
    draw_list->AddText(ImVec2(pos.x + 60.0f * scl, pos.y + 36.0f * scl),
                       color_u32(enabled ? p.muted : p.dim), meta);
    const ImU32 chevron = color_u32(enabled ? brighten(accent, 1.1f) : p.dim);
    const float cx = pos.x + width - 20.0f * scl;
    const float cy = pos.y + height * 0.5f;
    draw_list->AddLine(ImVec2(cx - 4.0f * scl, cy - 6.0f * scl), ImVec2(cx + 2.0f * scl, cy), chevron, 1.5f * scl);
    draw_list->AddLine(ImVec2(cx + 2.0f * scl, cy), ImVec2(cx - 4.0f * scl, cy + 6.0f * scl), chevron, 1.5f * scl);

    ImGui::PopID();
    return clicked;
}

void status_pill(const char* label, ImVec4 color) {
    const float scl = dpi_scale();
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(text_size.x + 20.0f * scl, 18.0f * scl);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                             color_u32(with_alpha(color, 0.15f)), 9.0f * scl);
    draw_list->AddCircleFilled(ImVec2(pos.x + 8.0f * scl, pos.y + 9.0f * scl), 2.5f * scl, color_u32(color));
    draw_list->AddText(ImVec2(pos.x + 14.0f * scl, pos.y + 3.0f * scl), color_u32(colors().muted), label);
    ImGui::Dummy(size);
}

void notice(const char* id, NoticeKind kind, const char* message) {
    const auto& p = colors();
    const float scl = dpi_scale();
    const ImVec4 color = notice_color(kind);
    ImGui::PushID(id);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, with_alpha(color, 0.1f));
    ImGui::PushStyleColor(ImGuiCol_Border, with_alpha(color, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scl, 9.0f * scl));
    ImGui::BeginChild("##notice", ImVec2(0, 44.0f * scl), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    status_pill(kind == NoticeKind::Error ? "ERROR" :
                kind == NoticeKind::Warning ? "WARN" :
                kind == NoticeKind::Success ? "OK" : "INFO", color);
    ImGui::SameLine();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextColored(p.text, "%s", message);
    ImGui::PopTextWrapPos();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    ImGui::PopID();
}

void kv_row_colored(const char* label, const char* value, ImVec4 value_color) {
    ImGui::TextColored(colors().dim, "%s", label);
    ImGui::SameLine(150.0f * dpi_scale());
    ImGui::TextColored(value_color, "%s", value);
}

void kv_row(const char* label, const char* value) {
    kv_row_colored(label, value, colors().text);
}

void kv_row(const char* label, const std::string& value) {
    kv_row(label, value.c_str());
}

bool file_path_row(const char* id, const char* label, const char* icon, char* buffer, size_t buffer_size,
                   const char* browse_label, FilePickerMode mode,
                   const nfdfilteritem_t* filters, int filter_count) {
    const float scl = dpi_scale();
    const float btn_w = 100.0f * scl;
    const float max_input_w = 500.0f * scl;

    ImGui::TextColored(colors().dim, "%s", label);
    ImGui::SameLine();

    const float remaining_w = ImGui::GetContentRegionAvail().x;
    float input_w = remaining_w - btn_w - ImGui::GetStyle().ItemSpacing.x;
    if (input_w > max_input_w) input_w = max_input_w;

    ImGui::SetNextItemWidth(input_w);
    ImGui::InputText((std::string("##") + id).c_str(), buffer, buffer_size);
    ImGui::SameLine();

    bool picked = false;
    if (soft_button(browse_label, ImVec2(btn_w, 0))) {
        nfdchar_t* out_path = nullptr;
        nfdresult_t result = NFD_ERROR;

        switch (mode) {
            case FilePickerMode::OpenFile:
                result = NFD_OpenDialog(&out_path, filters, filter_count, nullptr);
                break;
            case FilePickerMode::OpenFolder:
                result = NFD_PickFolder(&out_path, nullptr);
                break;
            case FilePickerMode::SaveFile:
                result = NFD_SaveDialog(&out_path, filters, filter_count, nullptr, nullptr);
                break;
        }

        if (result == NFD_OKAY && out_path) {
            std::strncpy(buffer, out_path, buffer_size - 1);
            buffer[buffer_size - 1] = '\0';
            NFD_FreePath(out_path);
            picked = true;
        }
    }
    return picked;
}

}
