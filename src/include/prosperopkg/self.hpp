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
#include <string>
#include <vector>

namespace prosperopkg {

struct SelfSegment {
    std::uint64_t flags = 0;
    std::uint64_t file_offset = 0;
    std::uint64_t file_size = 0;
    std::uint64_t mem_size = 0;

    [[nodiscard]] int id() const noexcept;
    [[nodiscard]] bool ordered() const noexcept;
    [[nodiscard]] bool encrypted() const noexcept;
    [[nodiscard]] bool signed_segment() const noexcept;
    [[nodiscard]] bool compressed() const noexcept;
    [[nodiscard]] bool blocked() const noexcept;
};

struct SelfExtInfo {
    std::uint64_t authority_id = 0;
    std::uint64_t program_type = 0;
    std::uint64_t app_version = 0;
    std::uint64_t firmware_version = 0;
    std::array<std::byte, 32> digest{};
};

struct SelfImage {
    std::uint32_t program_type = 0;
    int header_size = 0;
    int meta_size = 0;
    std::uint64_t file_size = 0;
    std::vector<SelfSegment> segments;
    std::vector<std::byte> elf;
    std::optional<SelfExtInfo> ext_info;
};

struct FselfOptions {
    std::uint64_t app_version = 0;
    std::uint64_t firmware_version = 0;
    std::optional<std::uint64_t> authority_id;
};

struct SelfLayout {
    static constexpr std::uint32_t magic = 0x1D3D154Fu;
    static constexpr std::size_t sce_header_size = 0x20;
    static constexpr std::size_t segment_entry_size = 0x20;
    static constexpr std::size_t ext_info_size = 0x40;
    static constexpr std::size_t elf_header_size = 0x40;
    static constexpr std::size_t elf_phdr_size = 0x38;
};

[[nodiscard]] bool is_self(std::span<const std::byte> data) noexcept;
[[nodiscard]] bool is_elf(std::span<const std::byte> data) noexcept;
[[nodiscard]] bool validate_self(std::span<const std::byte> data, std::string* error = nullptr);
[[nodiscard]] SelfImage parse_self(std::span<const std::byte> data);
[[nodiscard]] std::vector<std::byte> make_fself(
    std::span<const std::byte> elf,
    const FselfOptions& options = {});

} // namespace prosperopkg
