// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace prosperopkg {

constexpr std::size_t image_digest_block_size = 0x10000;
constexpr std::size_t image_digest_size = 32;
constexpr std::uint32_t digest_table_entry_id = 0x0001;
constexpr std::size_t package_digest_region_size = 0xFE0;
constexpr std::size_t package_digest_stored_offset = 0xFE0;
constexpr std::size_t cnt_header_rollup_stored_offset = 0x100;
constexpr std::size_t content_descriptor_size = 0x38;
constexpr std::size_t header_digest_prefix_size = 0x40;
constexpr std::size_t header_digest_mount_descriptor_size = 0x80;
constexpr std::uint64_t fih_relative_image_offset = 0x10000;

struct CntDigestPayload {
    std::uint32_t id = 0;
    std::span<const std::byte> payload{};
};

struct SblockDigestResult {
    std::size_t offset = 0;
    std::array<std::byte, image_digest_size> digest{};
};

[[nodiscard]] std::array<std::byte, image_digest_size> compute_sblock_digest(
    std::span<const std::byte> superblock_block);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_game_digest(
    std::span<const std::byte> superblock_block);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_fixed_info_digest(
    std::span<const std::byte> fih_header_block);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_body_digest(
    std::span<const std::byte> cnt_body);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_entry_digest(
    std::span<const std::byte> entry_payload);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_content_digest(
    std::span<const std::byte> content_descriptor,
    std::span<const std::byte> game_digest,
    std::span<const std::byte> major_param_digest,
    bool include_game);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_header_digest(
    std::span<const std::byte> cnt_header_prefix,
    std::span<const std::byte> mount_descriptor);

[[nodiscard]] std::array<std::byte, header_digest_mount_descriptor_size> force_fih_relative_image_offset(
    std::span<const std::byte> mount_descriptor);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_concat_digest(
    std::span<const std::array<std::byte, image_digest_size>> entry_digests);

[[nodiscard]] std::vector<std::byte> build_entry_digest_table(
    std::span<const CntDigestPayload> entries);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_package_digest(
    std::span<const std::byte> cnt);

[[nodiscard]] std::array<std::byte, image_digest_size> compute_cnt_header_rollup_digest(
    std::span<const std::byte> cnt);

[[nodiscard]] std::optional<std::size_t> locate_superblock(
    std::span<const std::byte> image,
    std::size_t block_size = image_digest_block_size);

[[nodiscard]] std::optional<SblockDigestResult> compute_sblock_digest_from_image(
    std::span<const std::byte> image,
    std::size_t block_size = image_digest_block_size);

} // namespace prosperopkg
