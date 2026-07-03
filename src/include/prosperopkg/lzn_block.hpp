// LibProsperoPkg - A library for building and inspecting PS5 packages.
// Copyright (C) 2026 seregonwar.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace prosperopkg {

enum class LznBlockCodec : std::uint16_t {
    store = 0,
    lzn1 = 1,
};

struct LznBlockOptions {
    LznBlockCodec codec = LznBlockCodec::lzn1;
    std::uint32_t block_size = 512u * 1024u;
    int level = 2;
};

struct LznBlockInfo {
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    LznBlockCodec codec = LznBlockCodec::store;
    std::uint32_t block_size = 0;
    std::uint32_t block_count = 0;
    std::uint64_t original_size = 0;
    std::uint64_t archive_size = 0;
    std::uint64_t index_offset = 0;
    std::uint32_t index_size = 0;
};

struct LznBlockEntry {
    std::uint64_t offset = 0;
    std::uint32_t encoded_size = 0;
    std::uint32_t decoded_size = 0;
    std::uint32_t checksum = 0;
    std::uint16_t flags = 0;

    [[nodiscard]] bool stored_raw() const noexcept { return (flags & 1u) != 0; }
};

[[nodiscard]] bool is_lzn_block_archive(std::span<const std::byte> data) noexcept;
[[nodiscard]] LznBlockInfo read_lzn_block_info(std::span<const std::byte> data);
[[nodiscard]] std::vector<LznBlockEntry> read_lzn_block_entries(std::span<const std::byte> data);

[[nodiscard]] std::vector<std::byte> lzn_block_compress(
    std::span<const std::byte> input,
    const LznBlockOptions& options = {});

[[nodiscard]] std::size_t lzn_block_decompress_to(
    std::span<const std::byte> archive,
    std::span<std::byte> output);

[[nodiscard]] std::vector<std::byte> lzn_block_decompress(std::span<const std::byte> archive);

[[nodiscard]] std::vector<std::byte> lzn_block_decompress_range(
    std::span<const std::byte> archive,
    std::uint64_t offset,
    std::size_t size);

} // namespace prosperopkg
