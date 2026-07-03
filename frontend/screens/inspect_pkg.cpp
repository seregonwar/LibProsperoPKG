#include "inspect_pkg.hpp"
#include "app/app_state.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <prosperopkg/pkg.hpp>
#include <prosperopkg/image_digests.hpp>

#include <imgui.h>
#include <nfd.h>

#include <fstream>
#include <string>
#include <filesystem>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace prospero::gui {

namespace {

struct InspectState {
    std::string pkg_path;
    bool loaded = false;
    std::string error;

    prosperopkg::Pkg pkg{};
    std::vector<std::byte> pkg_data;
};

InspectState& state() {
    static InspectState s;
    return s;
}

std::string bytes_to_hex(const std::array<std::byte, 32>& data) {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(b);
    }
    return oss.str();
}

void sync_pkg_to_workspace(const prosperopkg::Pkg& pkg, const std::string& path) {
    auto& app = get_app_state();
    auto& w = app.workspace;

    w.loaded_pkg_path = path;
    w.pkg_loaded = true;

    w.file_entries.clear();
    for (const auto& entry : pkg.entries) {
        WorkspaceFileEntry fe;
        fe.name = entry.name.empty() ? prosperopkg::to_string(entry.id) : entry.name;
        auto sz = entry.data_size;
        if (sz > 1024 * 1024)
            fe.size = std::to_string(sz / (1024 * 1024)) + " MB";
        else if (sz > 1024)
            fe.size = std::to_string(sz / 1024) + " KB";
        else
            fe.size = std::to_string(sz) + " B";
        fe.source = "PKG entry 0x" + [&]{
            std::ostringstream oss;
            oss << std::hex << std::setw(4) << std::setfill('0') << entry.raw_id;
            return oss.str();
        }();
        w.file_entries.push_back(std::move(fe));
    }

    if (!pkg.header) return;
    if (w.content_id.empty()) {
        w.content_id = pkg.header->content_id;
    }
}

void sync_digests_to_workspace(const std::vector<std::byte>& data) {
    auto& app = get_app_state();
    auto& w = app.workspace;

    w.digest_results.clear();
    try {
        auto pkg_digest = prosperopkg::compute_package_digest(data);
        WorkspaceDigest d;
        d.label = "Package";
        d.value = bytes_to_hex(pkg_digest);
        w.digest_results.push_back(std::move(d));
    } catch (...) {
    }
}

void load_pkg(const std::string& path) {
    auto& s = state();
    s.pkg_path = path;
    s.loaded = false;
    s.error.clear();

    try {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            s.error = "Could not open file: " + path;
            return;
        }

        s.pkg = prosperopkg::read_pkg(file);

        file.seekg(0, std::ios::end);
        const auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        s.pkg_data.resize(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char*>(s.pkg_data.data()),
                  static_cast<std::streamsize>(s.pkg_data.size()));

        s.loaded = true;
        get_app_state().status_message = "Loaded: " + std::filesystem::path(path).filename().string();

        sync_pkg_to_workspace(s.pkg, path);
        sync_digests_to_workspace(s.pkg_data);
    } catch (const std::exception& e) {
        s.error = e.what();
        get_app_state().status_message = "Error loading PKG";
    }
}

void draw_file_selector() {
    begin_panel("##InspectSource", "SOURCE", ImVec2(0, 130.0f * dpi_scale()));
    auto& s = state();
    static char path_buf[1024] = "";

    if (!s.pkg_path.empty() && std::strlen(path_buf) == 0) {
        std::strncpy(path_buf, s.pkg_path.c_str(), sizeof(path_buf) - 1);
    }

    nfdfilteritem_t filter[] = {{"PKG Files", "pkg"}};
    if (file_path_row("pkgpath", "PKG file", icons::kPackage, path_buf, sizeof(path_buf),
                      "Browse", FilePickerMode::OpenFile, filter, 1)) {
        s.pkg_path = path_buf;
        load_pkg(s.pkg_path);
    }

    ImGui::SameLine();
    if (primary_button("Load", ImVec2(84.0f * dpi_scale(), 0))) {
        s.pkg_path = path_buf;
        if (!s.pkg_path.empty()) {
            load_pkg(s.pkg_path);
        }
    }
    end_panel();
}

void draw_header_info() {
    auto& s = state();

    begin_panel("##PkgHeader", "PKG HEADER", ImVec2(0, 266.0f * dpi_scale()));

    if (ImGui::BeginTable("##header", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 200);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

        auto row = [](const char* field, const char* fmt, auto... args) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "%s", field);
            ImGui::TableSetColumnIndex(1);
            char buf[256];
            snprintf(buf, sizeof(buf), fmt, args...);
            ImGui::Text("%s", buf);
        };

        row("Type", "%s", prosperopkg::to_string(s.pkg.type));
        row("Content ID", "%s", s.pkg.header->content_id.c_str());
        row("Flags", "0x%08X", s.pkg.header->flags);
        row("Entry Count", "%u", s.pkg.header->entry_count);
        row("SC Entry Count", "%u", s.pkg.header->sc_entry_count);
        row("Entry Table Offset", "0x%08X", s.pkg.header->entry_table_offset);
        row("Body Offset", "0x%016llX", s.pkg.header->body_offset);
        row("Body Size", "%llu bytes (%.2f MB)",
            s.pkg.header->body_size, s.pkg.header->body_size / (1024.0 * 1024.0));
        row("DRM Type", "0x%08X", s.pkg.header->drm_type);
        row("Content Type", "0x%08X", s.pkg.header->content_type);

        ImGui::EndTable();
    }
    end_panel();
}

void draw_entries_list() {
    auto& s = state();

    begin_panel("##PkgEntries", "ENTRIES", ImVec2(0, 360.0f * dpi_scale()));
    ImGui::TextColored(colors().muted, "Total: %zu entries", s.pkg.entries.size());
    ImGui::Spacing();

    if (ImGui::BeginTable("##entries", 5,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, 0))) {

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Data Offset", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Data Size", ImGuiTableColumnFlags_WidthFixed, 120);
        ImGui::TableSetupColumn("Flags", ImGuiTableColumnFlags_WidthFixed, 100);
        ImGui::TableHeadersRow();

        for (const auto& entry : s.pkg.entries) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::Text("0x%04X (%s)", entry.raw_id, prosperopkg::to_string(entry.id));

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%s", entry.name.empty() ? "(unnamed)" : entry.name.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("0x%08X", entry.data_offset);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u bytes", entry.data_size);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s", entry.encrypted() ? "ENC" : "");
        }

        ImGui::EndTable();
    }
    end_panel();
}

void draw_digests_info() {
    auto& s = state();

    begin_panel("##PkgDigests", "DIGESTS", ImVec2(0, 126.0f * dpi_scale()));

    if (!s.pkg_data.empty()) {
        try {
            auto pkg_digest = prosperopkg::compute_package_digest(s.pkg_data);

            if (ImGui::BeginTable("##digests", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Digest", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.7f, 1.0f), "Package");
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", bytes_to_hex(pkg_digest).c_str());

                ImGui::EndTable();
            }
        } catch (const std::exception& e) {
            ImGui::TextColored(colors().danger, "Could not compute digest: %s", e.what());
        }
    }
    end_panel();
}

}

void draw_inspect_pkg_screen() {
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors().bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * dpi_scale(), 18.0f * dpi_scale()));

    ImGui::Begin("##InspectContent", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove);

    page_header("Inspect PKG", "Header, entries and package digest", "PKG");

    draw_file_selector();
    ImGui::Spacing();

    auto& s = state();
    if (!s.error.empty()) {
        notice("##InspectError", NoticeKind::Error, s.error.c_str());
    } else if (s.loaded) {
        draw_header_info();
        ImGui::Spacing();
        draw_entries_list();
        ImGui::Spacing();
        draw_digests_info();
    } else {
        notice("##InspectEmpty", NoticeKind::Info, "No package loaded.");
    }

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}
