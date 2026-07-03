#include "key_derivation.hpp"
#include "app/app_state.hpp"
#include "ui/ui_icons.hpp"
#include "ui/widgets.hpp"

#include <prosperopkg/pfs_keys.hpp>

#include <imgui.h>

#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <cstring>

namespace prospero::gui {

namespace {

struct KeyState {
    char content_id[48] = "";
    char passcode[64] = "";
    char seed_hex[128] = "";

    std::string ekpfs;
    std::string tweak_key;
    std::string data_key;
    std::string sign_key;

    std::string error;
    bool derived = false;
    bool initialized = false;
};

KeyState& state() {
    static KeyState s;
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

std::string bytes_to_hex(const std::array<std::byte, 16>& data) {
    std::ostringstream oss;
    for (auto b : data) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(b);
    }
    return oss.str();
}

std::vector<std::byte> hex_to_bytes(const char* hex) {
    std::vector<std::byte> result;
    size_t len = std::strlen(hex);
    if (len % 2 != 0) return result;

    for (size_t i = 0; i < len; i += 2) {
        char byte_str[3] = {hex[i], hex[i+1], 0};
        result.push_back(static_cast<std::byte>(std::stoi(byte_str, nullptr, 16)));
    }
    return result;
}

void do_derive() {
    auto& s = state();
    s.error.clear();
    s.derived = false;

    try {
        auto seed_bytes = hex_to_bytes(s.seed_hex);
        if (seed_bytes.empty()) {
            s.error = "Invalid seed hex string.";
            return;
        }

        auto ekpfs = prosperopkg::derive_ekpfs(s.content_id, s.passcode);
        auto encryption_keys = prosperopkg::derive_pfs_encryption_keys(ekpfs, seed_bytes);
        auto sign_key = prosperopkg::derive_pfs_sign_key(ekpfs, seed_bytes);

        s.ekpfs = bytes_to_hex(ekpfs);
        s.tweak_key = bytes_to_hex(encryption_keys.tweak_key);
        s.data_key = bytes_to_hex(encryption_keys.data_key);
        s.sign_key = bytes_to_hex(sign_key);
        s.derived = true;

        get_app_state().status_message = "Keys derived successfully";
    } catch (const std::exception& e) {
        s.error = e.what();
        get_app_state().status_message = "Key derivation failed";
    }
}

void draw_key_row(const char* label, const std::string& value) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextColored(colors().dim, "%s", label);
    ImGui::TableSetColumnIndex(1);
    ImGui::Text("%s", value.c_str());
    ImGui::TableSetColumnIndex(2);

    if (soft_button("Copy", ImVec2(60.0f * dpi_scale(), 0))) {
        ImGui::SetClipboardText(value.c_str());
    }
}

}

void draw_key_derivation_screen() {
    const float scl = dpi_scale();
    ImGui::PushStyleColor(ImGuiCol_WindowBg, colors().bg0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f * scl, 18.0f * scl));

    ImGui::Begin("##KeyContent", nullptr,
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove);

    auto& s = state();
    auto& app = get_app_state();
    auto& w = app.workspace;

    if (!s.initialized) {
        std::strncpy(s.content_id, w.content_id.c_str(), sizeof(s.content_id) - 1);
        s.content_id[sizeof(s.content_id) - 1] = '\0';
        std::strncpy(s.passcode, w.passcode.c_str(), sizeof(s.passcode) - 1);
        s.passcode[sizeof(s.passcode) - 1] = '\0';
        s.initialized = true;
    }

    page_header("Key Derivation", "EKPFS, PFS encryption and signing keys", "PFS");
    ImGui::Spacing();

    begin_panel("##KeyInput", "INPUT", ImVec2(0, 200.0f * scl));

    ImGui::TextColored(colors().dim, "Content ID");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(520.0f * scl);
    ImGui::InputTextWithHint("##contentid", "XX0000-PPSA00000_00-XXXXXXXXXXXXXXXX", s.content_id, sizeof(s.content_id));

    ImGui::TextColored(colors().dim, "Passcode");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(520.0f * scl);
    ImGui::InputTextWithHint("##passcode", "32-character passcode", s.passcode, sizeof(s.passcode), ImGuiInputTextFlags_Password);

    ImGui::TextColored(colors().dim, "Seed hex");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(520.0f * scl);
    ImGui::InputTextWithHint("##seed", "Hex seed from loaded PFS context", s.seed_hex, sizeof(s.seed_hex));
    end_panel();

    ImGui::Spacing();
    begin_panel("##KeyRun", "RUN", ImVec2(0, s.derived ? 352.0f * scl : 140.0f * scl));

    if (primary_button("Derive Keys", ImVec2(200.0f * scl, 36.0f * scl))) {
        if (std::strlen(s.content_id) > 0 && std::strlen(s.passcode) > 0 && std::strlen(s.seed_hex) > 0) {
            do_derive();
        } else {
            s.error = "Content ID, passcode, and seed are required.";
        }
    }

    if (!s.error.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(colors().danger, "%s", s.error.c_str());
    }

    if (s.derived) {
        ImGui::Spacing();
        ImGui::Spacing();
        section_label("DERIVED KEYS");

        if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 120);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableHeadersRow();

            draw_key_row("EKPFS", s.ekpfs);
            draw_key_row("Tweak Key", s.tweak_key);
            draw_key_row("Data Key", s.data_key);
            draw_key_row("Sign Key", s.sign_key);

            ImGui::EndTable();
        }
    }
    end_panel();

    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

}
