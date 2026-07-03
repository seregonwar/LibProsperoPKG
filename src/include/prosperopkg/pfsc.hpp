// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

namespace prosperopkg {

struct PfscInfo {
    std::uint32_t block_size = 0;
    std::uint64_t data_length = 0;
    std::uint64_t block_count = 0;
    std::uint64_t data_start = 0;
};

[[nodiscard]] bool is_pfsc_file(const std::filesystem::path& path);
[[nodiscard]] PfscInfo read_pfsc_info(const std::filesystem::path& path);

void pack_pfsc_raw(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::uint32_t block_size = 0x10000);

void pack_pfsc_zlib(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    int level = 9,
    std::uint32_t block_size = 0x10000);

[[nodiscard]] std::vector<std::byte> pack_pfsc_pfs_v3_stored(
    std::span<const std::byte> payload,
    int level = 7,
    std::uint32_t block_size = 0x40000);

void pack_pfsc_pfs_v3_stored(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    int level = 7,
    std::uint32_t block_size = 0x40000);

[[nodiscard]] std::uint64_t unpack_pfsc(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path);

} // namespace prosperopkg
