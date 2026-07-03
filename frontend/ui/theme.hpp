#pragma once

#include <imgui.h>

namespace prospero::gui {

struct Palette {
    ImVec4 bg0 = ImVec4(240.0f / 255.0f, 242.0f / 255.0f, 245.0f / 255.0f, 1.0f);
    ImVec4 bg1 = ImVec4(232.0f / 255.0f, 235.0f / 255.0f, 240.0f / 255.0f, 1.0f);
    ImVec4 bg2 = ImVec4(220.0f / 255.0f, 225.0f / 255.0f, 232.0f / 255.0f, 1.0f);
    ImVec4 bg3 = ImVec4(205.0f / 255.0f, 212.0f / 255.0f, 222.0f / 255.0f, 1.0f);
    ImVec4 rail = ImVec4(52.0f / 255.0f, 56.0f / 255.0f, 64.0f / 255.0f, 1.0f);
    ImVec4 panel = ImVec4(248.0f / 255.0f, 250.0f / 255.0f, 252.0f / 255.0f, 1.0f);
    ImVec4 panel2 = ImVec4(255.0f / 255.0f, 255.0f / 255.0f, 255.0f, 1.0f);
    ImVec4 border = ImVec4(140.0f / 255.0f, 150.0f / 255.0f, 165.0f / 255.0f, 1.0f);
    ImVec4 border_hot = ImVec4(14.0f / 255.0f, 120.0f / 255.0f, 200.0f / 255.0f, 1.0f);
    ImVec4 text = ImVec4(15.0f / 255.0f, 20.0f / 255.0f, 25.0f / 255.0f, 1.0f);
    ImVec4 muted = ImVec4(40.0f / 255.0f, 50.0f / 255.0f, 60.0f / 255.0f, 1.0f);
    ImVec4 dim = ImVec4(85.0f / 255.0f, 95.0f / 255.0f, 110.0f / 255.0f, 1.0f);
    ImVec4 primary = ImVec4(0.0f / 255.0f, 100.0f / 255.0f, 180.0f / 255.0f, 1.0f);
    ImVec4 primary2 = ImVec4(10.0f / 255.0f, 130.0f / 255.0f, 200.0f / 255.0f, 1.0f);
    ImVec4 primary_dark = ImVec4(0.0f / 255.0f, 70.0f / 255.0f, 130.0f / 255.0f, 1.0f);
    ImVec4 blue = ImVec4(50.0f / 255.0f, 100.0f / 255.0f, 170.0f / 255.0f, 1.0f);
    ImVec4 amber = ImVec4(170.0f / 255.0f, 110.0f / 255.0f, 20.0f / 255.0f, 1.0f);
    ImVec4 success = ImVec4(30.0f / 255.0f, 130.0f / 255.0f, 65.0f / 255.0f, 1.0f);
    ImVec4 danger = ImVec4(190.0f / 255.0f, 40.0f / 255.0f, 55.0f / 255.0f, 1.0f);
    ImVec4 violet = ImVec4(100.0f / 255.0f, 70.0f / 255.0f, 160.0f / 255.0f, 1.0f);
};

const Palette& colors();
ImU32 color_u32(const ImVec4& color);
ImVec4 with_alpha(ImVec4 color, float alpha);
ImVec4 clear_color();
void set_dpi_scale(float scale);
float dpi_scale();
void apply_theme();

}
