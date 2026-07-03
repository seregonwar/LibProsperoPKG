#include "pfsc_editor.hpp"
#include "app/app_state.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <prosperopkg/pfsc.hpp>

#include <imgui.h>
#include <nfd.h>

#include <string>
#include <filesystem>
#include <cstring>

namespace prospero::gui {

namespace {

struct PfscState {
    int mode = 0;
    char input_path[1024] = "";
    char output_path[1024] = "";
    int block_size_index = 2;
    int zlib_level = 9;

    std::string result;
    std::string error;
    bool working = false;
};

PfscState& state() {
    static PfscState s;
    return s;
}

const char* mode_names[] = {"Pack (raw)", "Pack (zlib)", "Pack (PFS v3 stored)", "Unpack"};
const char* block_size_names[] = {"4 KB", "8 KB", "16 KB", "32 KB", "64 KB", "256 KB", "1 MB"};
const uint32_t block_size_values[] = {0x1000, 0x2000, 0x4000, 0x8000, 0x10000, 0x40000, 0x100000};

void do_operation() {
    auto& s = state();
    s.result.clear();
    s.error.clear();
    s.working = true;

    try {
        const uint32_t bs = block_size_values[s.block_size_index];

        switch (s.mode) {
            case 0:
                prosperopkg::pack_pfsc_raw(s.input_path, s.output_path, bs);
                break;
            case 1:
                prosperopkg::pack_pfsc_zlib(s.input_path, s.output_path, s.zlib_level, bs);
                break;
            case 2:
                prosperopkg::pack_pfsc_pfs_v3_stored(s.input_path, s.output_path, 7, bs);
                break;
            case 3: {
                const auto written = prosperopkg::unpack_pfsc(s.input_path, s.output_path);
                s.result = "Unpacked " + std::to_string(written) + " bytes.";
                break;
            }
        }

        if (s.result.empty()) {
            s.result = "Operation completed successfully.";
        }
        get_app_state().status_message = "PFSC operation complete";
    } catch (const std::exception& e) {
        s.error = e.what();
        get_app_state().status_message = "PFSC operation failed";
    }

    s.working = false;
}

}

void draw_pfsc_editor_screen() {
    const float scl = dpi_scale();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors().bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scl, 18.0f * scl));

    ImGui::Begin("##PfscContent", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove);

    auto& s = state();

    page_header("PFSC Editor", "PFS container pack and unpack operations", "PFSC");
    ImGui::Spacing();

    begin_panel("##PfscOperation", "OPERATION", ImVec2(0, 130.0f * scl));

    ImGui::TextColored(colors().dim, "Mode");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(320.0f * scl);
    ImGui::Combo("##mode", &s.mode, mode_names, 4);
    end_panel();

    ImGui::Spacing();
    begin_panel("##PfscInput", "INPUT", ImVec2(0, 130.0f * scl));

    nfdfilteritem_t pfsc_filters[] = {
        {"PFSC Files", "pfsc,pfsv3"},
        {"All Files", "*"}
    };
    file_path_row("input", "Input file", icons::kFile, s.input_path, sizeof(s.input_path),
                  "Browse", FilePickerMode::OpenFile, pfsc_filters, 2);
    end_panel();

    ImGui::Spacing();
    begin_panel("##PfscOutput", "OUTPUT", ImVec2(0, 130.0f * scl));

    file_path_row("output", "Output file", icons::kOutput, s.output_path, sizeof(s.output_path),
                  "Browse", FilePickerMode::SaveFile);
    end_panel();

    if (s.mode < 3) {
        ImGui::Spacing();
        begin_panel("##PfscOptions", "OPTIONS", ImVec2(0, s.mode == 1 ? 164.0f * scl : 130.0f * scl));

        ImGui::TextColored(colors().dim, "Block size");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f * scl);
        ImGui::Combo("##blocksize", &s.block_size_index, block_size_names, 7);

        if (s.mode == 1) {
            ImGui::TextColored(colors().dim, "zlib level");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220.0f * scl);
            ImGui::SliderInt("##zliblevel", &s.zlib_level, 1, 9);
        }
        end_panel();
    }

    ImGui::Spacing();
    begin_panel("##PfscRun", "RUN", ImVec2(0, 150.0f * scl));

    if (s.working) {
        ImGui::TextColored(colors().amber, "Processing...");
    } else if (warning_button("Execute", ImVec2(200.0f * scl, 36.0f * scl))) {
        if (std::strlen(s.input_path) > 0 && std::strlen(s.output_path) > 0) {
            do_operation();
        } else {
            s.error = "Input and output paths are required.";
        }
    }

    if (!s.result.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(colors().success, "%s", s.result.c_str());
    }

    if (!s.error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(colors().danger, "%s", s.error.c_str());
    }
    end_panel();

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}
