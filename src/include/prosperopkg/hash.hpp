// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <span>

namespace prosperopkg {

[[nodiscard]] std::array<std::byte, 32> sha256(std::span<const std::byte> data);
[[nodiscard]] std::array<std::byte, 32> sha3_256(std::span<const std::byte> data);
[[nodiscard]] std::array<std::byte, 32> hmac_sha256(
    std::span<const std::byte> key,
    std::span<const std::byte> data);

} // namespace prosperopkg
