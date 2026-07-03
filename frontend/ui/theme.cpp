#include "theme.hpp"

#include <algorithm>

namespace prospero::gui {

namespace {

float g_dpi_scale = 1.0f;

}

const Palette& colors() {
    static Palette palette;
    return palette;
}

ImU32 color_u32(const ImVec4& color) {
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImVec4 with_alpha(ImVec4 color, float alpha) {
    color.w *= alpha;
    return color;
}

ImVec4 clear_color() {
    return colors().bg0;
}

void set_dpi_scale(float scale) {
    g_dpi_scale = std::clamp(scale, 0.5f, 4.0f);
}

float dpi_scale() {
    return g_dpi_scale;
}

void apply_theme() {
    const auto& p = colors();
    ImGuiStyle& style = ImGui::GetStyle();
    
    style.WindowRounding = 0.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.TabRounding = 3.0f;
    
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(8, 5);
    style.CellPadding = ImVec2(7, 4);
    style.ItemSpacing = ImVec2(7, 5);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 10.0f;
    
    ImVec4* colors = style.Colors;
    
    colors[ImGuiCol_Text] = p.text;
    colors[ImGuiCol_TextDisabled] = p.dim;
    colors[ImGuiCol_WindowBg] = p.bg0;
    colors[ImGuiCol_ChildBg] = p.panel;
    colors[ImGuiCol_PopupBg] = ImVec4(255.0f / 255.0f, 255.0f / 255.0f, 255.0f / 255.0f, 0.99f);
    colors[ImGuiCol_Border] = p.border;
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = p.panel2;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(236.0f / 255.0f, 246.0f / 255.0f, 252.0f / 255.0f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(214.0f / 255.0f, 235.0f / 255.0f, 248.0f / 255.0f, 1.00f);
    colors[ImGuiCol_TitleBg] = p.rail;
    colors[ImGuiCol_TitleBgActive] = p.rail;
    colors[ImGuiCol_TitleBgCollapsed] = with_alpha(p.rail, 0.75f);
    colors[ImGuiCol_MenuBarBg] = p.bg1;
    colors[ImGuiCol_ScrollbarBg] = with_alpha(p.bg1, 0.92f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(188.0f / 255.0f, 196.0f / 255.0f, 202.0f / 255.0f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(156.0f / 255.0f, 171.0f / 255.0f, 183.0f / 255.0f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = p.primary;
    colors[ImGuiCol_CheckMark] = p.primary2;
    colors[ImGuiCol_SliderGrab] = p.primary;
    colors[ImGuiCol_SliderGrabActive] = p.primary2;
    colors[ImGuiCol_Button] = p.bg2;
    colors[ImGuiCol_ButtonHovered] = p.bg3;
    colors[ImGuiCol_ButtonActive] = p.primary_dark;
    colors[ImGuiCol_Header] = ImVec4(224.0f / 255.0f, 237.0f / 255.0f, 247.0f / 255.0f, 1.00f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(207.0f / 255.0f, 228.0f / 255.0f, 243.0f / 255.0f, 1.00f);
    colors[ImGuiCol_HeaderActive] = ImVec4(185.0f / 255.0f, 216.0f / 255.0f, 239.0f / 255.0f, 1.00f);
    colors[ImGuiCol_Separator] = p.border;
    colors[ImGuiCol_SeparatorHovered] = p.primary;
    colors[ImGuiCol_SeparatorActive] = p.primary2;
    colors[ImGuiCol_ResizeGrip] = with_alpha(p.border, 0.50f);
    colors[ImGuiCol_ResizeGripHovered] = with_alpha(p.primary2, 0.80f);
    colors[ImGuiCol_ResizeGripActive] = p.primary;
    colors[ImGuiCol_Tab] = p.bg1;
    colors[ImGuiCol_TabHovered] = p.bg3;
    colors[ImGuiCol_TabActive] = p.panel2;
    colors[ImGuiCol_TabUnfocused] = p.bg1;
    colors[ImGuiCol_TabUnfocusedActive] = p.bg2;
    colors[ImGuiCol_PlotLines] = p.primary2;
    colors[ImGuiCol_PlotLinesHovered] = p.blue;
    colors[ImGuiCol_PlotHistogram] = p.amber;
    colors[ImGuiCol_PlotHistogramHovered] = p.primary2;
    colors[ImGuiCol_TableHeaderBg] = p.bg2;
    colors[ImGuiCol_TableBorderStrong] = p.border_hot;
    colors[ImGuiCol_TableBorderLight] = p.border;
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.00f, 0.36f, 0.78f, 0.035f);
    colors[ImGuiCol_TextSelectedBg] = with_alpha(p.primary2, 0.34f);
    colors[ImGuiCol_DragDropTarget] = with_alpha(p.primary2, 0.90f);
    colors[ImGuiCol_NavHighlight] = p.primary2;
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.16f, 0.18f, 0.20f, 0.35f);
}

}
