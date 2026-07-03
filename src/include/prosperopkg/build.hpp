// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace prosperopkg {

enum class BuildMode {
    application = 0,
    homebrew = 1,
    additional_content_data = 2,
    additional_content_no_data = 3,
};

enum class BuildOutputFormat {
    metadata_container = 0,
    debug_image = 1,
};

enum class InnerImageForm {
    plaintext = 0,
    encrypted = 1,
    compressed = 2,
    kraken_compressed = 3,
};

enum class InnerCompression {
    none = 0,
    zlib = 1,
    kraken = 2,
};

struct InnerImageBuildOptions {
    std::filesystem::path source_folder;
    std::filesystem::path output_path;
    std::string content_id;
    std::string passcode;
    InnerImageForm form = InnerImageForm::encrypted;
};

struct PackageBuildOptions {
    std::filesystem::path source_folder;
    std::filesystem::path output_folder;
    std::string content_id;
    std::string passcode;
    std::string title;
    std::string title_id;
    std::string version = "01.00";
    BuildMode mode = BuildMode::application;
    BuildOutputFormat output_format = BuildOutputFormat::debug_image;
    InnerCompression inner_compression = InnerCompression::none;
};

[[nodiscard]] std::filesystem::path build_inner_image(const InnerImageBuildOptions& options);
[[nodiscard]] std::filesystem::path build_package(const PackageBuildOptions& options);

} // namespace prosperopkg
