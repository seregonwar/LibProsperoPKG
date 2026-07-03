// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace prosperopkg {

struct UcpEntry {
    std::string name;
    std::vector<std::byte> data;
};

struct UcpLayout {
    static constexpr std::uint32_t magic = 0xB228C60Au;
    static constexpr std::uint32_t version = 1;
    static constexpr std::size_t header_size = 0x60;
    static constexpr std::size_t entry_record_size = 0x40;
    static constexpr std::size_t name_field_size = 0x20;
    static constexpr std::size_t count_offset = 0x10;
    static constexpr std::size_t record_size_offset = 0x14;
    static constexpr std::size_t digest_offset = 0x1C;
    static constexpr std::size_t digest_size = 20;
    static constexpr std::size_t blob_alignment = 0x10;
};

[[nodiscard]] bool is_ucp(std::span<const std::byte> data) noexcept;
[[nodiscard]] bool validate_ucp(std::span<const std::byte> data, std::string* error = nullptr);
[[nodiscard]] bool verify_ucp_digest(std::span<const std::byte> data);

[[nodiscard]] std::vector<UcpEntry> read_ucp(std::span<const std::byte> data);
[[nodiscard]] std::vector<std::byte> build_ucp(std::span<const UcpEntry> entries);
[[nodiscard]] std::vector<std::byte> build_ucp(const std::vector<UcpEntry>& entries);
[[nodiscard]] std::vector<std::byte> build_ucp_from_directory(const std::filesystem::path& directory);
[[nodiscard]] std::vector<std::byte> repair_ucp_digest(std::span<const std::byte> data);

} // namespace prosperopkg
