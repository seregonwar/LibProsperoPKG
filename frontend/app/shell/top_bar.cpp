#include "top_bar.hpp"
#include "app/app_state.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>
#include <nfd.h>
#include <cstdio>
#include <filesystem>
#include <string>

namespace prospero::gui {

namespace {

const char* screen_name(Screen s) {
    switch (s) {
        case Screen::Home: return "Home";
        case Screen::InspectPkg: return "Inspect PKG";
        case Screen::BuildPkg: return "Build PKG";
        case Screen::PfscEditor: return "PFSC Editor";
        case Screen::KeyDerivation: return "Key Derivation";
    }
    return "Unknown";
}

void document_tab(const char* label, bool selected) {
    const auto& p = colors();
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? p.panel2 : p.bg1);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.bg3);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.panel2);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::Button(label, ImVec2(208.0f * dpi_scale(), 23.0f * dpi_scale()));
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);
}

void editor_tab(Screen screen, const char* label) {
    auto& state = get_app_state();
    const auto& p = colors();
    const bool selected = state.current_screen == screen;
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? p.panel2 : p.bg1);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.bg3);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.panel2);
    ImGui::PushStyleColor(ImGuiCol_Text, selected ? p.primary_dark : p.text);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    if (ImGui::Button(label, ImVec2(112.0f * dpi_scale(), 26.0f * dpi_scale()))) {
        state.current_screen = screen;
    }
    if (selected) {
        const ImVec2 min = ImGui::GetItemRectMin();
        const ImVec2 max = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(min.x, max.y - 2.0f * dpi_scale()), max, color_u32(p.primary));
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
}

void open_pkg_dialog() {
    nfdchar_t* out_path = nullptr;
    nfdfilteritem_t filter[] = {{"PKG Files", "pkg"}};
    if (NFD_OpenDialog(&out_path, filter, 1, nullptr) == NFD_OKAY) {
        get_app_state().status_message = "Opening: ";
        get_app_state().status_message += std::filesystem::path(out_path).filename().string();
        get_app_state().current_screen = Screen::InspectPkg;
        NFD_FreePath(out_path);
    }
}

bool g_show_about = false;

void draw_about_popup() {
    if (g_show_about) {
        ImGui::OpenPopup("About LibProsperoPkg");
        g_show_about = false;
    }
    ImGui::SetNextWindowSize(ImVec2(380.0f * dpi_scale(), 0));
    if (ImGui::BeginPopupModal("About LibProsperoPkg", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("LibProsperoPkg v%s", PROSPEROPKG_VERSION);
        ImGui::Spacing();
        ImGui::TextWrapped("A library for building and inspecting PS5 packages.");
        ImGui::Spacing();
        ImGui::TextColored(colors().dim, "C++ port/rewrite Copyright (C) 2026 seregonwar");
        ImGui::TextColored(colors().dim, "Original C# LibProsperoPkg by SvenGDK");
        ImGui::Spacing();
        ImGui::TextColored(colors().dim, "Licensed under GPL-3.0-or-later");
        ImGui::Spacing();
        if (ImGui::Button("Close", ImVec2(120.0f * dpi_scale(), 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

}

void draw_top_bar() {
    const auto& p = colors();
    const float scl = dpi_scale();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.bg1);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scl, 4.0f * scl));

    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_MenuBar);

    const auto& state = get_app_state();

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open PKG...")) {
                open_pkg_dialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                get_app_state().request_exit = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project")) {
            if (ImGui::MenuItem("New Project")) {
                get_app_state().status_message = "New project created";
                get_app_state().workspace = SharedWorkspace{};
            }
            if (ImGui::MenuItem("Open Project...")) {
                get_app_state().status_message = "Open project not yet implemented";
            }
            if (ImGui::MenuItem("Save Project")) {
                get_app_state().status_message = "Save project not yet implemented";
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Package")) {
            if (ImGui::MenuItem("Inspect PKG")) {
                get_app_state().current_screen = Screen::InspectPkg;
            }
            if (ImGui::MenuItem("Build PKG")) {
                get_app_state().current_screen = Screen::BuildPkg;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("PFSC Editor")) {
                get_app_state().current_screen = Screen::PfscEditor;
            }
            if (ImGui::MenuItem("Key Derivation")) {
                get_app_state().current_screen = Screen::KeyDerivation;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("About LibProsperoPkg")) {
                g_show_about = true;
            }
            if (ImGui::MenuItem("Documentation")) {
                get_app_state().status_message = "Documentation not yet implemented";
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    draw_about_popup();

    char version[64];
    std::snprintf(version, sizeof(version), "v%s", PROSPEROPKG_VERSION);
    const float version_width = ImGui::CalcTextSize(version).x + 28.0f * scl;
    ImGui::SameLine(ImGui::GetWindowWidth() - version_width - 8.0f * scl);
    status_pill(version, p.primary2);

    ImGui::Separator();

    document_tab("No package loaded", state.current_screen == Screen::Home || state.current_screen == Screen::InspectPkg);
    ImGui::SameLine(0, 0);
    document_tab("Image workspace", state.current_screen == Screen::BuildPkg || state.current_screen == Screen::PfscEditor);
    ImGui::SameLine(0, 0);
    document_tab("Key material", state.current_screen == Screen::KeyDerivation);

    ImGui::SameLine();
    ImGui::TextColored(p.dim, "%s", screen_name(state.current_screen));
    ImGui::SameLine();
    ImGui::TextColored(p.success, "%s", state.status_message.c_str());

    ImGui::Separator();

    editor_tab(Screen::Home, "Workspace");
    ImGui::SameLine(0, 0);
    editor_tab(Screen::InspectPkg, "Info");
    ImGui::SameLine(0, 0);
    editor_tab(Screen::BuildPkg, "Build");
    ImGui::SameLine(0, 0);
    editor_tab(Screen::PfscEditor, "PFSC");
    ImGui::SameLine(0, 0);
    editor_tab(Screen::KeyDerivation, "Keys");

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}
