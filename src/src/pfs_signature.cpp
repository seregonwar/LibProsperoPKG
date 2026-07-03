// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/pfs_signature.hpp>

#include <prosperopkg/hash.hpp>

#include <algorithm>
#include <stdexcept>

namespace prosperopkg {
namespace {

void require_superblock_size(std::span<const std::byte> superblock)
{
    if (superblock.size() < pfs_superblock_icv_coverage) {
        throw std::invalid_argument("PFS superblock is too small for the ICV coverage region");
    }
}

void write_le32(std::span<std::byte> data, std::uint32_t value)
{
    if (data.size() < 4) {
        throw std::invalid_argument("Not enough space to write a little-endian u32");
    }
    data[0] = static_cast<std::byte>(value & 0xFFu);
    data[1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
    data[2] = static_cast<std::byte>((value >> 16u) & 0xFFu);
    data[3] = static_cast<std::byte>((value >> 24u) & 0xFFu);
}

} // namespace

std::array<std::byte, pfs_signature_hash_length> compute_outer_pfs_block_hash(
    std::span<const std::byte> plaintext_block)
{
    return sha3_256(plaintext_block);
}

std::array<std::byte, pfs_signature_hash_length> compute_superblock_icv(
    std::span<const std::byte> superblock)
{
    require_superblock_size(superblock);

    std::array<std::byte, pfs_superblock_icv_coverage> region{};
    std::copy_n(superblock.begin(), region.size(), region.begin());
    std::fill_n(region.begin() + static_cast<std::ptrdiff_t>(pfs_superblock_icv_offset),
                pfs_signature_hash_length,
                std::byte{0});
    return sha3_256(region);
}

void write_superblock_icv(std::span<std::byte> superblock)
{
    require_superblock_size(superblock);
    const auto icv = compute_superblock_icv(superblock);
    std::copy(
        icv.begin(),
        icv.end(),
        superblock.begin() + static_cast<std::ptrdiff_t>(pfs_superblock_icv_offset));
}

void write_superblock_root_hash(
    std::span<std::byte> superblock,
    std::span<const std::byte> block_hash,
    std::uint32_t block_index)
{
    if (superblock.size() < pfs_superblock_root_block_index_offset + 4) {
        throw std::invalid_argument("PFS superblock is too small for the super-root hash fields");
    }
    if (block_hash.size() != pfs_signature_hash_length) {
        throw std::invalid_argument("PFS block hash must be exactly 32 bytes");
    }

    std::copy(
        block_hash.begin(),
        block_hash.end(),
        superblock.begin() + static_cast<std::ptrdiff_t>(pfs_superblock_root_hash_offset));
    write_le32(superblock.subspan(pfs_superblock_root_block_index_offset, 4), block_index);
}

void write_superblock_root_hash_for_block(
    std::span<std::byte> superblock,
    std::span<const std::byte> inode_table_block,
    std::uint32_t block_index)
{
    write_superblock_root_hash(superblock, compute_outer_pfs_block_hash(inode_table_block), block_index);
}

} // namespace prosperopkg
