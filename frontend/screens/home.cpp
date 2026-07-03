#include "home.hpp"
#include "app/app_state.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <imgui.h>
#include <nfd.h>

#include <string>
#include <filesystem>

namespace prospero::gui {

namespace {

const char* package_kinds[] = {
    "PS5 Application",
    "PS5 Patch",
    "PS5 Add-on Content",
    "PS5 System Package",
};

void sync_workspace_to_fields(SharedWorkspace& w, char* content_id, char* title_id, char* title, char* passcode, int& package_kind, bool& include_build_timestamp) {
    std::strncpy(content_id, w.content_id.c_str(), 63);
    content_id[63] = '\0';
    std::strncpy(title_id, w.title_id.c_str(), 31);
    title_id[31] = '\0';
    std::strncpy(title, w.title.c_str(), 127);
    title[127] = '\0';
    std::strncpy(passcode, w.passcode.c_str(), 39);
    passcode[39] = '\0';
    package_kind = w.package_kind;
    include_build_timestamp = w.include_build_timestamp;
}

void sync_fields_to_workspace(SharedWorkspace& w, const char* content_id, const char* title_id, const char* title, const char* passcode, int package_kind, bool include_build_timestamp) {
    w.content_id = content_id;
    w.title_id = title_id;
    w.title = title;
    w.passcode = passcode;
    w.package_kind = package_kind;
    w.include_build_timestamp = include_build_timestamp;
}

void draw_package_panel(float height) {
    const float scl = dpi_scale();
    auto& app = get_app_state();
    auto& w = app.workspace;

    static char content_id[64] = "";
    static char title_id[32] = "";
    static char package_title[128] = "";
    static char passcode[40] = "";
    static int package_kind = 0;
    static bool include_build_timestamp = false;
    static bool initialized = false;

    if (!initialized) {
        sync_workspace_to_fields(w, content_id, title_id, package_title, passcode, package_kind, include_build_timestamp);
        initialized = true;
    }

    begin_panel("##WorkspacePackage", "PACKAGE METADATA", ImVec2(0, height));

    const float label_w = 100.0f * scl;
    const float field_w = 280.0f * scl;

    ImGui::TextColored(colors().dim, "Content ID");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    ImGui::InputTextWithHint("##ContentID", "XX0000-PPSA00000_00-XXXXXXXXXXXXXXXX", content_id, sizeof(content_id));
    ImGui::SameLine();
    ImGui::TextColored(colors().dim, "Package Kind");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f * scl);
    ImGui::Combo("##PackageKind", &package_kind, package_kinds, 4);

    ImGui::TextColored(colors().dim, "Title ID");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    ImGui::InputTextWithHint("##TitleID", "PPSA00000", title_id, sizeof(title_id));
    ImGui::SameLine();
    ImGui::Checkbox("Use build timestamp", &include_build_timestamp);

    ImGui::TextColored(colors().dim, "Title");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    ImGui::InputTextWithHint("##Title", "Package display title", package_title, sizeof(package_title));

    ImGui::TextColored(colors().dim, "Passcode");
    ImGui::SameLine(label_w);
    ImGui::SetNextItemWidth(field_w);
    ImGui::InputTextWithHint("##Passcode", "32-character passcode", passcode, sizeof(passcode), ImGuiInputTextFlags_Password);

    sync_fields_to_workspace(w, content_id, title_id, package_title, passcode, package_kind, include_build_timestamp);

    end_panel();
}

void draw_image_panel(float width, float height) {
    auto& app = get_app_state();
    auto& w = app.workspace;

    begin_panel("##WorkspaceImage", "PS5 IMAGE", ImVec2(width, height));

    if (w.pkg_loaded && !w.loaded_pkg_path.empty()) {
        ImGui::TextColored(colors().success, "Package loaded");
        ImGui::Spacing();
        ImGui::TextWrapped("%s", std::filesystem::path(w.loaded_pkg_path).filename().string().c_str());
    } else {
        ImGui::TextColored(colors().dim, "No package image loaded");
        ImGui::Spacing();
        ImGui::TextWrapped("Open a PKG in Inspect or configure a build source to populate the inner image tree.");
    }
    end_panel();
}

void draw_file_panel(float height) {
    auto& app = get_app_state();
    auto& w = app.workspace;
    const float scl = dpi_scale();

    begin_panel("##WorkspaceFiles", "FILES", ImVec2(0, height));

    if (!w.file_entries.empty()) {
        if (ImGui::BeginTable("##WorkspaceFilesTable", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
                              ImVec2(0, 120.0f * scl))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f * scl);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& entry : w.file_entries) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%s", entry.name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", entry.size.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::Text("%s", entry.source.c_str());
            }
            ImGui::EndTable();
        }
    } else {
        if (ImGui::BeginTable("##WorkspaceFilesTable", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable,
                              ImVec2(0, 80.0f * scl))) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 70.0f * scl);
            ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            ImGui::EndTable();
        }
        ImGui::TextColored(colors().dim, "No file entries available.");
    }
    ImGui::Spacing();

    static char source_buf[1024] = "";
    if (file_path_row("BuildSource", "Build source", icons::kFolder, source_buf, sizeof(source_buf),
                      "Browse", FilePickerMode::OpenFolder)) {
        w.source_folder = source_buf;
        w.file_entries.clear();

        std::filesystem::path src(source_buf);
        if (std::filesystem::exists(src) && std::filesystem::is_directory(src)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
                if (entry.is_regular_file()) {
                    WorkspaceFileEntry fe;
                    fe.name = entry.path().filename().string();
                    auto sz = entry.file_size();
                    if (sz > 1024 * 1024)
                        fe.size = std::to_string(sz / (1024 * 1024)) + " MB";
                    else if (sz > 1024)
                        fe.size = std::to_string(sz / 1024) + " KB";
                    else
                        fe.size = std::to_string(sz) + " B";
                    fe.source = std::filesystem::relative(entry.path(), src).string();
                    w.file_entries.push_back(std::move(fe));
                }
            }
        }
        app.status_message = "Loaded " + std::to_string(w.file_entries.size()) + " files from build source";
    }

    end_panel();
}

void draw_validation_panel(float height) {
    auto& app = get_app_state();
    auto& w = app.workspace;
    const float scl = dpi_scale();

    begin_panel("##WorkspaceValidation", "VALIDATION", ImVec2(0, height));

    if (!w.digest_results.empty()) {
        if (ImGui::BeginTable("##DigestTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Digest", ImGuiTableColumnFlags_WidthFixed, 120.0f * scl);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (const auto& d : w.digest_results) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(colors().dim, "%s", d.label.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", d.value.c_str());
            }
            ImGui::EndTable();
        }
    } else {
        ImGui::TextColored(colors().dim, "No digest results");
        ImGui::Spacing();
        ImGui::TextWrapped("Load a PS5 PKG to compute and inspect package, body, table and image digests.");
    }
    end_panel();
}

}

void draw_home_screen() {
    const float scl = dpi_scale();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors().bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f * scl, 8.0f * scl));

    ImGui::Begin("##HomeContent", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove);

    draw_package_panel(160.0f * scl);
    ImGui::Spacing();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float left_w = 260.0f * scl;
    const float right_h = (avail.y - 6.0f * scl) * 0.45f;

    draw_image_panel(left_w, avail.y);
    ImGui::SameLine();
    ImGui::BeginGroup();
    draw_file_panel(right_h);
    ImGui::Spacing();
    draw_validation_panel(0);
    ImGui::EndGroup();

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}
