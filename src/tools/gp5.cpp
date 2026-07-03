// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/prosperopkg.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

[[nodiscard]] prosperopkg::Gp5VolumeType parse_type(std::string_view value)
{
    if (value == "prospero_app" || value == "app") {
        return prosperopkg::Gp5VolumeType::prospero_app;
    }
    if (value == "prospero_patch" || value == "patch") {
        return prosperopkg::Gp5VolumeType::prospero_patch;
    }
    if (value == "prospero_ac" || value == "ac") {
        return prosperopkg::Gp5VolumeType::prospero_ac;
    }
    if (value == "prospero_ac_nodata" || value == "ac_nodata") {
        return prosperopkg::Gp5VolumeType::prospero_ac_nodata;
    }
    throw std::invalid_argument("Unknown GP5 volume type.");
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 3) {
            std::cerr << "usage: prosperopkg-gp5 <source-folder> <output.gp5> [--flat] [--type <type>] [--passcode <passcode>]\n";
            return 2;
        }

        const std::filesystem::path source = argv[1];
        const std::filesystem::path output = argv[2];
        bool flat = false;
        auto type = prosperopkg::Gp5VolumeType::prospero_app;
        std::string passcode = "00000000000000000000000000000000";

        for (int i = 3; i < argc; ++i) {
            const std::string_view arg(argv[i]);
            if (arg == "--flat") {
                flat = true;
            } else if (arg == "--type" && i + 1 < argc) {
                type = parse_type(argv[++i]);
            } else if (arg == "--passcode" && i + 1 < argc) {
                passcode = argv[++i];
            } else {
                throw std::invalid_argument("Unknown or incomplete option.");
            }
        }

        const auto project = flat
            ? prosperopkg::gp5_from_folder_explicit(source, type, passcode)
            : prosperopkg::gp5_from_folder(source, type, passcode);
        prosperopkg::write_gp5_file(project, output);
        std::cout << "Wrote GP5 project: " << output.string() << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "prosperopkg-gp5: " << ex.what() << '\n';
        return 1;
    }
}
