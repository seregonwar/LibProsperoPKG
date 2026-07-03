// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace prosperopkg {

inline constexpr std::uint32_t crc32c_reflected_polynomial = 0x82F63B78u;

[[nodiscard]] std::uint32_t crc32c_update(
    std::uint32_t crc,
    std::span<const std::byte> data) noexcept;

[[nodiscard]] std::uint32_t crc32c(std::span<const std::byte> data) noexcept;

[[nodiscard]] std::uint32_t crc32c_bytes(
    const void* data,
    std::size_t size) noexcept;

} // namespace prosperopkg
