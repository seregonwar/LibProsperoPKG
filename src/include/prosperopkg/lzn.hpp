// LibProsperoPkg - A library for building and inspecting PS5 packages.
// Copyright (C) 2026 seregonwar.

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace prosperopkg {

struct LznFrameInfo {
    std::uint16_t version = 0;
    std::uint16_t flags = 0;
    std::uint64_t original_size = 0;
    std::uint64_t payload_size = 0;

    [[nodiscard]] bool stored_raw() const noexcept { return (flags & 1u) != 0; }
};

[[nodiscard]] bool is_lzn_frame(std::span<const std::byte> data) noexcept;
[[nodiscard]] LznFrameInfo read_lzn_frame_info(std::span<const std::byte> data);

[[nodiscard]] std::vector<std::byte> lzn_compress(
    std::span<const std::byte> input,
    int level = 1);

[[nodiscard]] std::size_t lzn_decompress_to(
    std::span<const std::byte> frame,
    std::span<std::byte> output);

[[nodiscard]] std::vector<std::byte> lzn_decompress(std::span<const std::byte> frame);

} // namespace prosperopkg
