#include "app/app_state.hpp"
#include "app/shell/window.hpp"
#include "app/shell/top_bar.hpp"
#include "app/shell/status_bar.hpp"
#include "screens/home.hpp"
#include "screens/inspect_pkg.hpp"
#include "screens/build_pkg.hpp"
#include "screens/pfsc_editor.hpp"
#include "screens/key_derivation.hpp"
#include "ui/theme.hpp"
#include "ui/fonts.hpp"
#include "ui/file_picker.hpp"

#include <imgui.h>

#include <cstdio>

int main(int, char**) {
    prospero::gui::Window window;

    if (!window.init(1400, 900, "LibProsperoPkg")) {
        fprintf(stderr, "Failed to initialize window\n");
        return 1;
    }

    prospero::gui::set_dpi_scale(window.get_dpi_scale());
    prospero::gui::apply_theme();
    prospero::gui::load_fonts(window.get_dpi_scale());

    if (!prospero::gui::init_file_dialog()) {
        fprintf(stderr, "Failed to initialize file dialogs\n");
        return 1;
    }

    auto& state = prospero::gui::get_app_state();
    state.dpi_scale = window.get_dpi_scale();

    const float top_bar_height = 140.0f * state.dpi_scale;
    const float status_bar_height = 28.0f * state.dpi_scale;

    while (!window.should_close() && !state.request_exit) {
        window.poll_events();
        window.begin_frame();

        const float window_width = ImGui::GetIO().DisplaySize.x;
        const float window_height = ImGui::GetIO().DisplaySize.y;
        const float content_height = window_height - top_bar_height - status_bar_height;

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(window_width, top_bar_height));
        prospero::gui::draw_top_bar();

        ImGui::SetNextWindowPos(ImVec2(0, top_bar_height));
        ImGui::SetNextWindowSize(ImVec2(window_width, content_height));

        switch (state.current_screen) {
            case prospero::gui::Screen::Home:
                prospero::gui::draw_home_screen();
                break;
            case prospero::gui::Screen::InspectPkg:
                prospero::gui::draw_inspect_pkg_screen();
                break;
            case prospero::gui::Screen::BuildPkg:
                prospero::gui::draw_build_pkg_screen();
                break;
            case prospero::gui::Screen::PfscEditor:
                prospero::gui::draw_pfsc_editor_screen();
                break;
            case prospero::gui::Screen::KeyDerivation:
                prospero::gui::draw_key_derivation_screen();
                break;
        }

        ImGui::SetNextWindowPos(ImVec2(0, window_height - status_bar_height));
        ImGui::SetNextWindowSize(ImVec2(window_width, status_bar_height));
        prospero::gui::draw_status_bar();

        window.end_frame();
    }

    prospero::gui::shutdown_file_dialog();
    window.shutdown();

    return 0;
}
