// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <prosperopkg/pfs_keys.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace prosperopkg {

constexpr std::size_t pfs_inner_xts_sector_size = 0x1000;
constexpr std::size_t pfs_outer_default_block_size = 0x10000;
constexpr std::uint64_t pfs_outer_signed_block_tweak_flag = 0x800000000000ull;

enum class OuterPfsBlockKind : std::uint8_t {
    data,
    signed_block,
    plaintext,
};

[[nodiscard]] std::size_t outer_pfs_metadata_block_index(
    std::uint64_t image_offset,
    std::uint64_t metadata_offset,
    std::size_t block_size = pfs_outer_default_block_size);

[[nodiscard]] std::uint64_t outer_pfs_block_sector(
    std::size_t block_index,
    OuterPfsBlockKind kind);

[[nodiscard]] std::size_t transform_inner_pfs_image(
    std::span<std::byte> image,
    const PfsEncryptionKeys& keys,
    bool encrypt,
    std::size_t pfs_block_size = pfs_outer_default_block_size);

[[nodiscard]] std::size_t transform_outer_pfs_image(
    std::span<std::byte> image,
    const PfsEncryptionKeys& keys,
    std::size_t block_size,
    std::span<const OuterPfsBlockKind> block_kinds,
    bool encrypt);

[[nodiscard]] std::size_t transform_outer_pfs_image(
    std::span<std::byte> image,
    const PfsEncryptionKeys& keys,
    std::size_t block_size,
    int plaintext_block_index,
    bool encrypt);

} // namespace prosperopkg
