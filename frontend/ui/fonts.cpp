#include "fonts.hpp"

#include <imgui.h>
#include <cmath>

namespace prospero::gui {

void load_fonts(float dpi_scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig config;
    config.OversampleH = 3;
    config.OversampleV = 3;
    config.PixelSnapH = true;

#ifdef __APPLE__
    const char* font_path = "/System/Library/Fonts/Helvetica.ttc";
#else
    const char* font_path = nullptr;
#endif

    const float base_size = std::round(14.0f * dpi_scale);
    config.SizePixels = base_size;

    if (font_path) {
        io.Fonts->AddFontFromFileTTF(font_path, base_size, &config);
    } else {
        io.Fonts->AddFontDefault(&config);
    }

    io.Fonts->Build();
}

}
