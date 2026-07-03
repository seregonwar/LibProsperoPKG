// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/image_digests.hpp>

#include <prosperopkg/hash.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace prosperopkg {
namespace {

constexpr std::size_t cnt_rollup_size_field_offset = 0x1C;
constexpr std::size_t cnt_rollup_offset_field_offset = 0x20;
constexpr std::size_t cnt_pfs_image_offset_field = 0x410;
constexpr std::array<std::byte, 4> superblock_magic{
    std::byte{0x0B}, std::byte{0x2A}, std::byte{0x33}, std::byte{0x01}};

void require_size(std::span<const std::byte> data, std::size_t expected, const char* name)
{
    if (data.size() != expected) {
        throw std::invalid_argument(std::string(name) + " has an invalid size");
    }
}

[[nodiscard]] std::uint32_t read_be32(std::span<const std::byte> data)
{
    if (data.size() < 4) {
        throw std::invalid_argument("Not enough bytes to read a big-endian u32");
    }
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           static_cast<std::uint32_t>(data[3]);
}

[[nodiscard]] std::uint64_t read_be64(std::span<const std::byte> data)
{
    if (data.size() < 8) {
        throw std::invalid_argument("Not enough bytes to read a big-endian u64");
    }
    return (static_cast<std::uint64_t>(read_be32(data.first(4))) << 32u) |
           read_be32(data.subspan(4, 4));
}

[[nodiscard]] std::uint64_t read_le64(std::span<const std::byte> data)
{
    if (data.size() < 8) {
        throw std::invalid_argument("Not enough bytes to read a little-endian u64");
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[i]) << (i * 8u);
    }
    return value;
}

void write_be64(std::span<std::byte> data, std::uint64_t value)
{
    if (data.size() < 8) {
        throw std::invalid_argument("Not enough bytes to write a big-endian u64");
    }
    for (std::size_t i = 0; i < 8; ++i) {
        const int shift = static_cast<int>((7u - i) * 8u);
        data[i] = static_cast<std::byte>((value >> shift) & 0xFFu);
    }
}

[[nodiscard]] std::array<std::byte, image_digest_size> digest_checked_block(
    std::span<const std::byte> block,
    const char* name)
{
    require_size(block, image_digest_block_size, name);
    return sha3_256(block);
}

} // namespace

std::array<std::byte, image_digest_size> compute_sblock_digest(
    std::span<const std::byte> superblock_block)
{
    return digest_checked_block(superblock_block, "superblock block");
}

std::array<std::byte, image_digest_size> compute_game_digest(
    std::span<const std::byte> superblock_block)
{
    return compute_sblock_digest(superblock_block);
}

std::array<std::byte, image_digest_size> compute_fixed_info_digest(
    std::span<const std::byte> fih_header_block)
{
    return digest_checked_block(fih_header_block, "FIH header block");
}

std::array<std::byte, image_digest_size> compute_body_digest(std::span<const std::byte> cnt_body)
{
    return sha3_256(cnt_body);
}

std::array<std::byte, image_digest_size> compute_entry_digest(std::span<const std::byte> entry_payload)
{
    return sha3_256(entry_payload);
}

std::array<std::byte, image_digest_size> compute_content_digest(
    std::span<const std::byte> content_descriptor,
    std::span<const std::byte> game_digest,
    std::span<const std::byte> major_param_digest,
    bool include_game)
{
    require_size(content_descriptor, content_descriptor_size, "content descriptor");
    if (include_game) {
        require_size(game_digest, image_digest_size, "game digest");
    }
    require_size(major_param_digest, image_digest_size, "major parameter digest");

    std::vector<std::byte> preimage;
    preimage.reserve(content_descriptor_size + (include_game ? image_digest_size : 0u) + image_digest_size);
    preimage.insert(preimage.end(), content_descriptor.begin(), content_descriptor.end());
    if (include_game) {
        preimage.insert(preimage.end(), game_digest.begin(), game_digest.end());
    }
    preimage.insert(preimage.end(), major_param_digest.begin(), major_param_digest.end());
    return sha3_256(preimage);
}

std::array<std::byte, image_digest_size> compute_header_digest(
    std::span<const std::byte> cnt_header_prefix,
    std::span<const std::byte> mount_descriptor)
{
    require_size(cnt_header_prefix, header_digest_prefix_size, "CNT header prefix");
    require_size(mount_descriptor, header_digest_mount_descriptor_size, "mount descriptor");

    std::array<std::byte, header_digest_prefix_size + header_digest_mount_descriptor_size> preimage{};
    std::copy(cnt_header_prefix.begin(), cnt_header_prefix.end(), preimage.begin());
    std::copy(
        mount_descriptor.begin(),
        mount_descriptor.end(),
        preimage.begin() + static_cast<std::ptrdiff_t>(header_digest_prefix_size));
    return sha3_256(preimage);
}

std::array<std::byte, header_digest_mount_descriptor_size> force_fih_relative_image_offset(
    std::span<const std::byte> mount_descriptor)
{
    require_size(mount_descriptor, header_digest_mount_descriptor_size, "mount descriptor");
    std::array<std::byte, header_digest_mount_descriptor_size> patched{};
    std::copy(mount_descriptor.begin(), mount_descriptor.end(), patched.begin());
    write_be64(std::span<std::byte>(patched).subspan(cnt_pfs_image_offset_field - 0x400, 8), fih_relative_image_offset);
    return patched;
}

std::array<std::byte, image_digest_size> compute_concat_digest(
    std::span<const std::array<std::byte, image_digest_size>> entry_digests)
{
    std::vector<std::byte> preimage;
    preimage.reserve(entry_digests.size() * image_digest_size);
    for (const auto& digest : entry_digests) {
        preimage.insert(preimage.end(), digest.begin(), digest.end());
    }
    return sha3_256(preimage);
}

std::vector<std::byte> build_entry_digest_table(std::span<const CntDigestPayload> entries)
{
    std::vector<std::byte> table(entries.size() * image_digest_size);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].id == digest_table_entry_id) {
            continue;
        }
        const auto digest = sha3_256(entries[i].payload);
        std::copy(
            digest.begin(),
            digest.end(),
            table.begin() + static_cast<std::ptrdiff_t>(i * image_digest_size));
    }
    return table;
}

std::array<std::byte, image_digest_size> compute_package_digest(std::span<const std::byte> cnt)
{
    if (cnt.size() < package_digest_region_size) {
        throw std::invalid_argument("CNT is too small for the package digest region");
    }
    return sha3_256(cnt.first(package_digest_region_size));
}

std::array<std::byte, image_digest_size> compute_cnt_header_rollup_digest(std::span<const std::byte> cnt)
{
    if (cnt.size() < cnt_rollup_offset_field_offset + 8) {
        throw std::invalid_argument("CNT is too small for the rollup header fields");
    }

    const std::uint32_t size = read_be32(cnt.subspan(cnt_rollup_size_field_offset, 4));
    const std::uint64_t offset = read_be64(cnt.subspan(cnt_rollup_offset_field_offset, 8));
    if (offset > cnt.size() || static_cast<std::uint64_t>(size) > cnt.size() - offset) {
        throw std::invalid_argument("CNT rollup region is outside the supplied CNT");
    }
    return sha3_256(cnt.subspan(static_cast<std::size_t>(offset), size));
}

std::optional<std::size_t> locate_superblock(std::span<const std::byte> image, std::size_t block_size)
{
    if (block_size <= 0x10) {
        return std::nullopt;
    }

    for (std::size_t offset = 0; offset + block_size <= image.size(); offset += block_size) {
        if (read_le64(image.subspan(offset, 8)) == 2u &&
            std::equal(superblock_magic.begin(), superblock_magic.end(), image.begin() + static_cast<std::ptrdiff_t>(offset + 8))) {
            return offset;
        }
    }
    return std::nullopt;
}

std::optional<SblockDigestResult> compute_sblock_digest_from_image(
    std::span<const std::byte> image,
    std::size_t block_size)
{
    const auto offset = locate_superblock(image, block_size);
    if (!offset) {
        return std::nullopt;
    }
    if (*offset + image_digest_block_size > image.size()) {
        return std::nullopt;
    }
    return SblockDigestResult{*offset, compute_sblock_digest(image.subspan(*offset, image_digest_block_size))};
}

} // namespace prosperopkg
