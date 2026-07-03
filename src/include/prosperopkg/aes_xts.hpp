// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace prosperopkg {

constexpr std::size_t aes128_key_size = 16;
constexpr std::size_t aes_block_size = 16;

[[nodiscard]] std::array<std::byte, aes_block_size> aes128_encrypt_block(
    std::span<const std::byte> key,
    std::span<const std::byte> block);

[[nodiscard]] std::array<std::byte, aes_block_size> aes128_decrypt_block(
    std::span<const std::byte> key,
    std::span<const std::byte> block);

void xts_transform_unit(
    std::span<std::byte> data_unit,
    std::span<const std::byte> data_key,
    std::span<const std::byte> tweak_key,
    std::uint64_t sector_number,
    bool encrypt);

} // namespace prosperopkg
