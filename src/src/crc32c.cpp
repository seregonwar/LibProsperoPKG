// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/crc32c.hpp>

#include <array>

namespace prosperopkg {
namespace {

[[nodiscard]] constexpr std::array<std::uint32_t, 256> build_table() noexcept
{
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t n = 0; n < table.size(); ++n) {
        std::uint32_t c = n;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1u) != 0u ? crc32c_reflected_polynomial ^ (c >> 1u) : c >> 1u;
        }
        table[n] = c;
    }
    return table;
}

constexpr auto table = build_table();

} // namespace

std::uint32_t crc32c_update(std::uint32_t crc, std::span<const std::byte> data) noexcept
{
    std::uint32_t c = crc;
    for (std::byte byte : data) {
        c = table[(c ^ static_cast<std::uint8_t>(byte)) & 0xFFu] ^ (c >> 8u);
    }
    return c;
}

std::uint32_t crc32c(std::span<const std::byte> data) noexcept
{
    return ~crc32c_update(0xFFFFFFFFu, data);
}

std::uint32_t crc32c_bytes(const void* data, std::size_t size) noexcept
{
    const auto* bytes = static_cast<const std::byte*>(data);
    return crc32c(std::span<const std::byte>(bytes, size));
}

} // namespace prosperopkg
