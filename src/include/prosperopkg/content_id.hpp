// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace prosperopkg {

enum class PackageMode {
    application,
    homebrew,
    additional_content_data,
    additional_content_no_data,
};

[[nodiscard]] bool is_valid_content_id(std::string_view content_id);
[[nodiscard]] bool is_valid_title_id(std::string_view title_id);

[[nodiscard]] std::string compose_content_id(
    std::string_view publisher = "UP9000",
    std::string_view title_id = "PPSA00000",
    std::string_view label = "");

[[nodiscard]] bool is_dlc_mode(PackageMode mode) noexcept;

} // namespace prosperopkg
