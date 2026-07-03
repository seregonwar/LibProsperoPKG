#include "build_pkg.hpp"
#include "app/app_state.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <prosperopkg/build.hpp>

#include <imgui.h>
#include <nfd.h>

#include <string>
#include <filesystem>
#include <cstring>

namespace prospero::gui {

namespace {

struct BuildState {
    char source_folder[1024] = "";
    char content_id[48] = "";
    char passcode[64] = "";
    char output_folder[1024] = "";

    int compression_index = 0;

    std::string result;
    std::string error;
    bool building = false;
    bool initialized = false;
};

BuildState& state() {
    static BuildState s;
    return s;
}

const char* compression_names[] = {"None", "zlib", "Kraken"};
const prosperopkg::InnerCompression compression_values[] = {
    prosperopkg::InnerCompression::none,
    prosperopkg::InnerCompression::zlib,
    prosperopkg::InnerCompression::kraken,
};

void sync_workspace_to_build(SharedWorkspace& w, BuildState& s) {
    std::strncpy(s.source_folder, w.source_folder.c_str(), sizeof(s.source_folder) - 1);
    s.source_folder[sizeof(s.source_folder) - 1] = '\0';
    std::strncpy(s.content_id, w.content_id.c_str(), sizeof(s.content_id) - 1);
    s.content_id[sizeof(s.content_id) - 1] = '\0';
    std::strncpy(s.passcode, w.passcode.c_str(), sizeof(s.passcode) - 1);
    s.passcode[sizeof(s.passcode) - 1] = '\0';
    std::strncpy(s.output_folder, w.output_folder.c_str(), sizeof(s.output_folder) - 1);
    s.output_folder[sizeof(s.output_folder) - 1] = '\0';
    s.compression_index = w.compression_index;
}

void sync_build_to_workspace(BuildState& s, SharedWorkspace& w) {
    w.source_folder = s.source_folder;
    w.content_id = s.content_id;
    w.passcode = s.passcode;
    w.output_folder = s.output_folder;
    w.compression_index = s.compression_index;
}

void do_build() {
    auto& s = state();
    auto& app = get_app_state();
    auto& w = app.workspace;

    s.result.clear();
    s.error.clear();
    s.building = true;

    try {
        prosperopkg::PackageBuildOptions options;
        options.source_folder = s.source_folder;
        options.content_id = s.content_id;
        options.passcode = s.passcode;
        options.output_folder = s.output_folder;
        if (options.output_folder.empty()) options.output_folder = ".";
        options.title = w.title;
        options.title_id = w.title_id;
        options.inner_compression = compression_values[s.compression_index];

        const auto output = prosperopkg::build_package(options);
        s.result = output.string();

        sync_build_to_workspace(s, w);
        app.status_message = "Built: " + std::filesystem::path(output).filename().string();
    } catch (const std::exception& e) {
        s.error = e.what();
        app.status_message = "Build failed";
    }

    s.building = false;
}

}

void draw_build_pkg_screen() {
    const float scl = dpi_scale();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors().bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scl, 18.0f * scl));

    ImGui::Begin("##BuildContent", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove);

    auto& s = state();
    auto& app = get_app_state();
    auto& w = app.workspace;

    if (!s.initialized) {
        sync_workspace_to_build(w, s);
        s.initialized = true;
    }

    page_header("Build PKG", "Package assembly and image compression", "BUILD");
    ImGui::Spacing();

    begin_panel("##BuildSource", "SOURCE", ImVec2(0, 130.0f * scl));

    file_path_row("source", "Source folder", icons::kFolder, s.source_folder, sizeof(s.source_folder),
                  "Browse", FilePickerMode::OpenFolder);
    end_panel();

    ImGui::Spacing();
    begin_panel("##BuildConfig", "PACKAGE CONFIGURATION", ImVec2(0, 180.0f * scl));

    ImGui::TextColored(colors().dim, "Content ID");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(460.0f * scl);
    ImGui::InputTextWithHint("##contentid", "XX0000-PPSA00000_00-XXXXXXXXXXXXXXXX", s.content_id, sizeof(s.content_id));

    ImGui::TextColored(colors().dim, "Passcode");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(460.0f * scl);
    ImGui::InputTextWithHint("##passcode", "32-character passcode", s.passcode, sizeof(s.passcode), ImGuiInputTextFlags_Password);

    ImGui::Spacing();

    ImGui::TextColored(colors().dim, "Inner compression");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260.0f * scl);
    ImGui::Combo("##compression", &s.compression_index, compression_names, 3);
    end_panel();

    ImGui::Spacing();
    begin_panel("##BuildOutput", "OUTPUT", ImVec2(0, 130.0f * scl));

    file_path_row("output", "Output folder", icons::kOutput, s.output_folder, sizeof(s.output_folder),
                  "Browse", FilePickerMode::OpenFolder);
    end_panel();

    ImGui::Spacing();
    begin_panel("##BuildRun", "RUN", ImVec2(0, 150.0f * scl));

    if (s.building) {
        ImGui::TextColored(colors().amber, "Building...");
    } else if (primary_button("Build Package", ImVec2(210.0f * scl, 36.0f * scl))) {
        if (std::strlen(s.source_folder) > 0 && std::strlen(s.content_id) > 0) {
            do_build();
        } else {
            s.error = "Source folder and content ID are required.";
        }
    }

    if (!s.result.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(colors().success, "Success");
        kv_row("Output", s.result);
    }

    if (!s.error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(colors().danger, "%s", s.error.c_str());
    }
    end_panel();

    sync_build_to_workspace(s, w);

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}
