// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/pfs_image.hpp>

#include <prosperopkg/aes_xts.hpp>

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace prosperopkg {
namespace {

void validate_keys(const PfsEncryptionKeys& keys)
{
    (void)keys;
}

void validate_block_size(std::size_t block_size)
{
    if (block_size == 0 || (block_size % aes_block_size) != 0) {
        throw std::invalid_argument("PFS block size must be a positive multiple of 16 bytes");
    }
}

[[nodiscard]] std::size_t block_count(std::size_t size, std::size_t block_size)
{
    return (size + block_size - 1u) / block_size;
}

} // namespace

std::size_t outer_pfs_metadata_block_index(
    std::uint64_t image_offset,
    std::uint64_t metadata_offset,
    std::size_t block_size)
{
    validate_block_size(block_size);
    if (metadata_offset < image_offset) {
        throw std::invalid_argument("PFS metadata offset must not precede the image offset");
    }
    const std::uint64_t relative = metadata_offset - image_offset;
    if ((relative % block_size) != 0) {
        throw std::invalid_argument("PFS metadata offset is not block-aligned within the image");
    }
    return static_cast<std::size_t>(relative / block_size);
}

std::uint64_t outer_pfs_block_sector(std::size_t block_index, OuterPfsBlockKind kind)
{
    if (block_index > 0xFFFFFFFFull) {
        throw std::invalid_argument("PFS outer block index is too large for the sector encoding");
    }
    std::uint64_t sector = static_cast<std::uint64_t>(block_index);
    switch (kind) {
    case OuterPfsBlockKind::data:
        return sector;
    case OuterPfsBlockKind::signed_block:
        return sector | pfs_outer_signed_block_tweak_flag;
    case OuterPfsBlockKind::plaintext:
        return sector;
    }
    throw std::invalid_argument("Unknown outer PFS block kind");
}

std::size_t transform_inner_pfs_image(
    std::span<std::byte> image,
    const PfsEncryptionKeys& keys,
    bool encrypt,
    std::size_t pfs_block_size)
{
    validate_keys(keys);
    validate_block_size(pfs_block_size);
    if ((pfs_block_size % pfs_inner_xts_sector_size) != 0) {
        throw std::invalid_argument("Inner PFS block size must be a multiple of the XTS sector size");
    }
    if ((image.size() % pfs_inner_xts_sector_size) != 0) {
        throw std::invalid_argument("Inner PFS image size must be a multiple of the XTS sector size");
    }

    const std::size_t start_sector = pfs_block_size / pfs_inner_xts_sector_size;
    const std::size_t total_sectors = image.size() / pfs_inner_xts_sector_size;
    std::size_t transformed = 0;
    for (std::size_t sector = start_sector; sector < total_sectors; ++sector) {
        auto unit = image.subspan(sector * pfs_inner_xts_sector_size, pfs_inner_xts_sector_size);
        xts_transform_unit(unit, keys.data_key, keys.tweak_key, static_cast<std::uint64_t>(sector), encrypt);
        ++transformed;
    }
    return transformed;
}

std::size_t transform_outer_pfs_image(
    std::span<std::byte> image,
    const PfsEncryptionKeys& keys,
    std::size_t block_size,
    std::span<const OuterPfsBlockKind> block_kinds,
    bool encrypt)
{
    validate_keys(keys);
    validate_block_size(block_size);
    const std::size_t total = block_count(image.size(), block_size);
    if (block_kinds.size() != total) {
        throw std::invalid_argument("Outer PFS block kind count must match the image block count");
    }

    std::size_t transformed = 0;
    for (std::size_t block = 0; block < total; ++block) {
        const OuterPfsBlockKind kind = block_kinds[block];
        if (kind == OuterPfsBlockKind::plaintext) {
            continue;
        }

        const std::size_t offset = block * block_size;
        const std::size_t length = std::min(block_size, image.size() - offset);
        if ((length % aes_block_size) != 0) {
            throw std::invalid_argument("Outer PFS block length must be a multiple of 16 bytes");
        }

        xts_transform_unit(
            image.subspan(offset, length),
            keys.data_key,
            keys.tweak_key,
            outer_pfs_block_sector(block, kind),
            encrypt);
        ++transformed;
    }
    return transformed;
}

std::size_t transform_outer_pfs_image(
    std::span<std::byte> image,
    const PfsEncryptionKeys& keys,
    std::size_t block_size,
    int plaintext_block_index,
    bool encrypt)
{
    validate_block_size(block_size);
    const std::size_t total = block_count(image.size(), block_size);
    std::vector<OuterPfsBlockKind> kinds(total, OuterPfsBlockKind::data);
    if (plaintext_block_index >= 0) {
        const auto index = static_cast<std::size_t>(plaintext_block_index);
        if (index >= total) {
            throw std::invalid_argument("Plaintext outer PFS block index is outside the image");
        }
        kinds[index] = OuterPfsBlockKind::plaintext;
    }
    return transform_outer_pfs_image(image, keys, block_size, kinds, encrypt);
}

} // namespace prosperopkg
