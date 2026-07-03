// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace prosperopkg {

enum class PackageType {
    meta,
    full_retail,
    full_debug,
};

enum class EntryId : std::uint32_t {
    unknown = 0x0000,
    digests = 0x0001,
    entry_keys = 0x0010,
    image_key = 0x0020,
    general_digests = 0x0080,
    metas = 0x0100,
    entry_names = 0x0200,
    license_dat = 0x0400,
    license_info = 0x0401,
    param_json = 0x1000,
    param_sfo = 0x1001,
    playgo_chunk_dat = 0x1300,
    playgo_chunk_sha = 0x1301,
    playgo_manifest_xml = 0x1302,
    icon0_png = 0x1200,
    pic0_png = 0x1220,
    snd0_at9 = 0x1240,
    icon0_dds = 0x1280,
    pic0_dds = 0x12A0,
    pic1_dds = 0x12C0,
    pic2_dds = 0x2060,
};

struct PkgLayout {
    static constexpr std::array<std::byte, 4> cnt_magic{
        std::byte{0x7F}, std::byte{'C'}, std::byte{'N'}, std::byte{'T'}};
    static constexpr std::array<std::byte, 4> fih_magic{
        std::byte{0x7F}, std::byte{'F'}, std::byte{'I'}, std::byte{'H'}};

    static constexpr std::uint32_t entry_flag_encrypted = 0x80000000u;
    static constexpr std::size_t entry_meta_size = 0x20;
    static constexpr std::size_t header_size = 0x5A0;
    static constexpr std::size_t content_id_size = 0x30;

    static constexpr std::size_t fih_header_region_size = 0x10000;
    static constexpr std::size_t fih_signed_byte_offset = 0x05;
    static constexpr std::size_t fih_pfs_image_offset_field = 0x10;
    static constexpr std::size_t fih_pfs_image_size_field = 0x18;
    static constexpr std::size_t fih_embedded_cnt_offset_field = 0x58;
};

struct PkgHeader {
    std::array<std::byte, 4> magic{};
    std::uint32_t flags = 0;
    std::uint32_t entry_count = 0;
    std::uint16_t sc_entry_count = 0;
    std::uint32_t entry_table_offset = 0;
    std::uint64_t body_offset = 0;
    std::uint64_t body_size = 0;
    std::string content_id;
    std::uint32_t drm_type = 0;
    std::uint32_t content_type = 0;
};

struct PkgEntry {
    EntryId id = EntryId::unknown;
    std::uint32_t raw_id = 0;
    std::uint32_t name_table_offset = 0;
    std::uint32_t flags1 = 0;
    std::uint32_t flags2 = 0;
    std::uint32_t data_offset = 0;
    std::uint32_t data_size = 0;
    std::string name;

    [[nodiscard]] std::uint32_t key_index() const noexcept;
    [[nodiscard]] bool encrypted() const noexcept;
};

struct FihHeader {
    std::uint8_t signed_byte = 0;
    std::uint64_t pfs_image_offset = 0;
    std::uint64_t pfs_image_size = 0;
    std::uint64_t embedded_cnt_offset = 0;

    [[nodiscard]] bool is_official() const noexcept;
};

struct Pkg {
    PackageType type = PackageType::meta;
    std::optional<PkgHeader> header;
    std::vector<PkgEntry> entries;
    std::optional<FihHeader> fih;
};

struct PkgWriterEntry {
    std::uint32_t id = 0;
    std::string name;
    std::vector<std::byte> data;
    std::uint32_t flags1 = 0;
    std::uint32_t flags2 = 0;
};

struct PkgWriterOptions {
    std::string content_id;
    std::uint32_t flags = 0;
    std::uint32_t drm_type = 0;
    std::uint32_t content_type = 0;
    std::uint16_t sc_entry_count = 0;
    std::vector<PkgWriterEntry> entries;
};

[[nodiscard]] const char* to_string(PackageType type) noexcept;
[[nodiscard]] const char* to_string(EntryId id) noexcept;
[[nodiscard]] EntryId entry_id_from_raw(std::uint32_t raw) noexcept;

[[nodiscard]] std::optional<PackageType> detect_type(std::istream& stream);
[[nodiscard]] std::optional<PackageType> detect_type(const std::filesystem::path& path);

[[nodiscard]] Pkg read_pkg(std::istream& stream);
[[nodiscard]] Pkg read_pkg(const std::filesystem::path& path);

[[nodiscard]] std::vector<std::byte> write_cnt(const PkgWriterOptions& options);
void write_cnt_file(const PkgWriterOptions& options, const std::filesystem::path& path);

} // namespace prosperopkg
