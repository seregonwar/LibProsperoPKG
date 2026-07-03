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

constexpr std::size_t pfs_signature_hash_length = 32;
constexpr std::size_t pfs_superblock_icv_coverage = 0x5A0;
constexpr std::size_t pfs_superblock_icv_offset = 0x380;
constexpr std::size_t pfs_superblock_root_hash_offset = 0xB8;
constexpr std::size_t pfs_superblock_root_block_index_offset = 0xD8;

[[nodiscard]] std::array<std::byte, pfs_signature_hash_length> compute_outer_pfs_block_hash(
    std::span<const std::byte> plaintext_block);

[[nodiscard]] std::array<std::byte, pfs_signature_hash_length> compute_superblock_icv(
    std::span<const std::byte> superblock);

void write_superblock_icv(std::span<std::byte> superblock);

void write_superblock_root_hash(
    std::span<std::byte> superblock,
    std::span<const std::byte> block_hash,
    std::uint32_t block_index);

void write_superblock_root_hash_for_block(
    std::span<std::byte> superblock,
    std::span<const std::byte> inode_table_block,
    std::uint32_t block_index);

} // namespace prosperopkg
