#include "status_bar.hpp"
#include "app/app_state.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>

namespace prospero::gui {

void draw_status_bar() {
    const auto& p = colors();
    const float scl = dpi_scale();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f * scl, 4.0f * scl));
    
    ImGui::Begin("##StatusBar", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar);
    
    const auto& state = get_app_state();
    
    ImGui::TextColored(p.dim, "%.1f FPS", ImGui::GetIO().Framerate);
    
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    
    ImGui::TextColored(p.dim, "DPI %.0f%%", state.dpi_scale * 100.0f);
    
    const char* label = "LibProsperoPkg";
    const float label_width = ImGui::CalcTextSize(label).x + 24.0f * scl;
    ImGui::SameLine(ImGui::GetWindowWidth() - label_width);
    ImGui::TextColored(p.muted, "%s  %s", icons::kPackage, label);
    
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}
