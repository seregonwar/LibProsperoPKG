// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/pkg.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <ios>
#include <limits>
#include <map>
#include <stdexcept>

namespace prosperopkg {
namespace {

[[nodiscard]] std::uint16_t read_be16(const std::byte* data) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(data[0]) << 8u) |
        static_cast<std::uint16_t>(data[1]));
}

[[nodiscard]] std::uint32_t read_be32(const std::byte* data) noexcept
{
    return (static_cast<std::uint32_t>(data[0]) << 24u) |
           (static_cast<std::uint32_t>(data[1]) << 16u) |
           (static_cast<std::uint32_t>(data[2]) << 8u) |
           static_cast<std::uint32_t>(data[3]);
}

[[nodiscard]] std::uint64_t read_be64(const std::byte* data) noexcept
{
    return (static_cast<std::uint64_t>(read_be32(data)) << 32u) | read_be32(data + 4);
}

[[nodiscard]] std::uint32_t read_le32(const std::byte* data) noexcept
{
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

[[nodiscard]] std::uint64_t read_le64(const std::byte* data) noexcept
{
    return static_cast<std::uint64_t>(read_le32(data)) |
           (static_cast<std::uint64_t>(read_le32(data + 4)) << 32u);
}

void write_be16(std::byte* data, std::uint16_t value) noexcept
{
    data[0] = static_cast<std::byte>((value >> 8u) & 0xFFu);
    data[1] = static_cast<std::byte>(value & 0xFFu);
}

void write_be32(std::byte* data, std::uint32_t value) noexcept
{
    data[0] = static_cast<std::byte>((value >> 24u) & 0xFFu);
    data[1] = static_cast<std::byte>((value >> 16u) & 0xFFu);
    data[2] = static_cast<std::byte>((value >> 8u) & 0xFFu);
    data[3] = static_cast<std::byte>(value & 0xFFu);
}

void write_be64(std::byte* data, std::uint64_t value) noexcept
{
    write_be32(data, static_cast<std::uint32_t>(value >> 32u));
    write_be32(data + 4, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
}

[[nodiscard]] std::uint32_t align_up(std::uint32_t value, std::uint32_t alignment)
{
    return ((value + alignment - 1u) / alignment) * alignment;
}

[[nodiscard]] std::uint64_t stream_length(std::istream& stream)
{
    stream.clear();
    const auto original = stream.tellg();
    if (original == std::istream::pos_type(-1)) {
        throw std::runtime_error("Stream is not seekable.");
    }

    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end == std::istream::pos_type(-1)) {
        throw std::runtime_error("Could not determine stream length.");
    }

    stream.seekg(original, std::ios::beg);
    return static_cast<std::uint64_t>(end);
}

void seek_abs(std::istream& stream, std::uint64_t offset)
{
    if (offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("Offset is too large for this platform stream.");
    }

    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!stream) {
        throw std::runtime_error("Could not seek inside PS5 PKG stream.");
    }
}

void read_exact(std::istream& stream, std::byte* out, std::size_t size)
{
    stream.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(size));
    if (stream.gcount() != static_cast<std::streamsize>(size)) {
        throw std::runtime_error("Unexpected end of PS5 PKG while reading container.");
    }
}

[[nodiscard]] std::vector<std::byte> read_exact_at(
    std::istream& stream,
    std::uint64_t offset,
    std::size_t size)
{
    std::vector<std::byte> buffer(size);
    seek_abs(stream, offset);
    read_exact(stream, buffer.data(), buffer.size());
    return buffer;
}

[[nodiscard]] bool starts_with(
    const std::array<std::byte, 4>& actual,
    const std::array<std::byte, 4>& expected) noexcept
{
    return std::equal(actual.begin(), actual.end(), expected.begin());
}

[[nodiscard]] std::string read_nul_trimmed_ascii(const std::byte* data, std::size_t size)
{
    std::size_t len = 0;
    while (len < size && data[len] != std::byte{0}) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(data), len);
}

[[nodiscard]] FihHeader read_fih_header(std::istream& stream)
{
    auto buffer = read_exact_at(stream, 0, 0x100);
    return FihHeader{
        static_cast<std::uint8_t>(buffer[PkgLayout::fih_signed_byte_offset]),
        read_le64(buffer.data() + PkgLayout::fih_pfs_image_offset_field),
        read_le64(buffer.data() + PkgLayout::fih_pfs_image_size_field),
        read_le64(buffer.data() + PkgLayout::fih_embedded_cnt_offset_field)};
}

[[nodiscard]] PkgHeader read_header(std::istream& stream, std::uint64_t base_offset)
{
    auto buffer = read_exact_at(stream, base_offset, PkgLayout::header_size);
    PkgHeader header{};
    std::copy_n(buffer.begin(), 4, header.magic.begin());
    header.flags = read_be32(buffer.data() + 0x04);
    header.entry_count = read_be32(buffer.data() + 0x10);
    header.sc_entry_count = read_be16(buffer.data() + 0x14);
    header.entry_table_offset = read_be32(buffer.data() + 0x18);
    header.body_offset = read_be64(buffer.data() + 0x20);
    header.body_size = read_be64(buffer.data() + 0x28);
    header.content_id = read_nul_trimmed_ascii(buffer.data() + 0x40, PkgLayout::content_id_size);
    header.drm_type = read_be32(buffer.data() + 0x70);
    header.content_type = read_be32(buffer.data() + 0x74);
    return header;
}

[[nodiscard]] std::vector<PkgEntry> read_entry_table(
    std::istream& stream,
    const PkgHeader& header,
    std::uint64_t base_offset)
{
    const std::uint64_t length = stream_length(stream);
    const std::uint64_t table_offset = base_offset + header.entry_table_offset;
    if (table_offset > length) {
        throw std::runtime_error("PS5 PKG entry table starts outside the stream.");
    }

    const std::uint64_t max_entries =
        (length - table_offset) / static_cast<std::uint64_t>(PkgLayout::entry_meta_size);
    if (header.entry_count > max_entries || header.entry_count > 0x10000u) {
        throw std::runtime_error("PS5 PKG entry table is malformed (entry count out of range).");
    }

    std::vector<PkgEntry> entries;
    entries.reserve(header.entry_count);
    auto record = std::array<std::byte, PkgLayout::entry_meta_size>{};
    seek_abs(stream, table_offset);

    for (std::uint32_t i = 0; i < header.entry_count; ++i) {
        read_exact(stream, record.data(), record.size());
        const std::uint32_t raw_id = read_be32(record.data());
        entries.push_back(PkgEntry{
            entry_id_from_raw(raw_id),
            raw_id,
            read_be32(record.data() + 0x04),
            read_be32(record.data() + 0x08),
            read_be32(record.data() + 0x0C),
            read_be32(record.data() + 0x10),
            read_be32(record.data() + 0x14),
            {}});
    }

    return entries;
}

void resolve_names(std::istream& stream, std::vector<PkgEntry>& entries, std::uint64_t base_offset)
{
    const auto it = std::find_if(entries.begin(), entries.end(), [](const PkgEntry& entry) {
        return entry.id == EntryId::entry_names;
    });
    if (it == entries.end() || it->data_size == 0) {
        return;
    }

    auto names = read_exact_at(stream, base_offset + it->data_offset, it->data_size);
    for (PkgEntry& entry : entries) {
        if (entry.name_table_offset == 0 || entry.name_table_offset >= names.size()) {
            continue;
        }

        std::size_t start = entry.name_table_offset;
        std::size_t end = start;
        while (end < names.size() && names[end] != std::byte{0}) {
            ++end;
        }

        entry.name.assign(
            reinterpret_cast<const char*>(names.data() + start),
            end - start);
    }
}

void write_ascii(std::vector<std::byte>& image, std::uint32_t offset, const std::string& value)
{
    if (offset + value.size() > image.size()) {
        throw std::runtime_error("Internal CNT writer offset overflow.");
    }
    std::memcpy(image.data() + offset, value.data(), value.size());
}

} // namespace

std::uint32_t PkgEntry::key_index() const noexcept
{
    return (flags2 & 0xF000u) >> 12u;
}

bool PkgEntry::encrypted() const noexcept
{
    return (flags1 & PkgLayout::entry_flag_encrypted) != 0u;
}

bool FihHeader::is_official() const noexcept
{
    return signed_byte == 0x80u;
}

const char* to_string(PackageType type) noexcept
{
    switch (type) {
    case PackageType::meta:
        return "Meta";
    case PackageType::full_retail:
        return "FullRetail";
    case PackageType::full_debug:
        return "FullDebug";
    }
    return "Unknown";
}

const char* to_string(EntryId id) noexcept
{
    switch (id) {
    case EntryId::unknown:
        return "Unknown";
    case EntryId::digests:
        return "Digests";
    case EntryId::entry_keys:
        return "EntryKeys";
    case EntryId::image_key:
        return "ImageKey";
    case EntryId::general_digests:
        return "GeneralDigests";
    case EntryId::metas:
        return "Metas";
    case EntryId::entry_names:
        return "EntryNames";
    case EntryId::license_dat:
        return "LicenseDat";
    case EntryId::license_info:
        return "LicenseInfo";
    case EntryId::param_json:
        return "ParamJson";
    case EntryId::param_sfo:
        return "ParamSfo";
    case EntryId::playgo_chunk_dat:
        return "PlaygoChunkDat";
    case EntryId::playgo_chunk_sha:
        return "PlaygoChunkSha";
    case EntryId::playgo_manifest_xml:
        return "PlaygoManifestXml";
    case EntryId::icon0_png:
        return "Icon0Png";
    case EntryId::pic0_png:
        return "Pic0Png";
    case EntryId::snd0_at9:
        return "Snd0At9";
    case EntryId::icon0_dds:
        return "Icon0Dds";
    case EntryId::pic0_dds:
        return "Pic0Dds";
    case EntryId::pic1_dds:
        return "Pic1Dds";
    case EntryId::pic2_dds:
        return "Pic2Dds";
    }
    return "Unknown";
}

EntryId entry_id_from_raw(std::uint32_t raw) noexcept
{
    switch (raw) {
    case static_cast<std::uint32_t>(EntryId::digests):
        return EntryId::digests;
    case static_cast<std::uint32_t>(EntryId::entry_keys):
        return EntryId::entry_keys;
    case static_cast<std::uint32_t>(EntryId::image_key):
        return EntryId::image_key;
    case static_cast<std::uint32_t>(EntryId::general_digests):
        return EntryId::general_digests;
    case static_cast<std::uint32_t>(EntryId::metas):
        return EntryId::metas;
    case static_cast<std::uint32_t>(EntryId::entry_names):
        return EntryId::entry_names;
    case static_cast<std::uint32_t>(EntryId::license_dat):
        return EntryId::license_dat;
    case static_cast<std::uint32_t>(EntryId::license_info):
        return EntryId::license_info;
    case static_cast<std::uint32_t>(EntryId::param_json):
        return EntryId::param_json;
    case static_cast<std::uint32_t>(EntryId::param_sfo):
        return EntryId::param_sfo;
    case static_cast<std::uint32_t>(EntryId::playgo_chunk_dat):
        return EntryId::playgo_chunk_dat;
    case static_cast<std::uint32_t>(EntryId::playgo_chunk_sha):
        return EntryId::playgo_chunk_sha;
    case static_cast<std::uint32_t>(EntryId::playgo_manifest_xml):
        return EntryId::playgo_manifest_xml;
    case static_cast<std::uint32_t>(EntryId::icon0_png):
        return EntryId::icon0_png;
    case static_cast<std::uint32_t>(EntryId::pic0_png):
        return EntryId::pic0_png;
    case static_cast<std::uint32_t>(EntryId::snd0_at9):
        return EntryId::snd0_at9;
    case static_cast<std::uint32_t>(EntryId::icon0_dds):
        return EntryId::icon0_dds;
    case static_cast<std::uint32_t>(EntryId::pic0_dds):
        return EntryId::pic0_dds;
    case static_cast<std::uint32_t>(EntryId::pic1_dds):
        return EntryId::pic1_dds;
    case static_cast<std::uint32_t>(EntryId::pic2_dds):
        return EntryId::pic2_dds;
    default:
        return EntryId::unknown;
    }
}

std::optional<PackageType> detect_type(std::istream& stream)
{
    if (stream_length(stream) < 6) {
        return std::nullopt;
    }

    std::array<std::byte, 4> magic{};
    seek_abs(stream, 0);
    read_exact(stream, magic.data(), magic.size());

    if (starts_with(magic, PkgLayout::cnt_magic)) {
        return PackageType::meta;
    }

    if (starts_with(magic, PkgLayout::fih_magic)) {
        std::array<std::byte, 1> signed_byte{};
        seek_abs(stream, PkgLayout::fih_signed_byte_offset);
        read_exact(stream, signed_byte.data(), signed_byte.size());
        if (signed_byte[0] == std::byte{0x80}) {
            return PackageType::full_retail;
        }
        if (signed_byte[0] == std::byte{0x00}) {
            return PackageType::full_debug;
        }
    }

    return std::nullopt;
}

std::optional<PackageType> detect_type(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open PS5 PKG file for reading: " + path.string());
    }
    return detect_type(file);
}

Pkg read_pkg(std::istream& stream)
{
    auto type = detect_type(stream);
    if (!type) {
        throw std::runtime_error("Not a recognisable PS5 PKG (unknown magic).");
    }

    if (*type == PackageType::meta) {
        PkgHeader header = read_header(stream, 0);
        auto entries = read_entry_table(stream, header, 0);
        resolve_names(stream, entries, 0);
        return Pkg{*type, std::move(header), std::move(entries), std::nullopt};
    }

    FihHeader fih = read_fih_header(stream);
    const std::uint64_t length = stream_length(stream);
    if (fih.embedded_cnt_offset == 0 ||
        fih.embedded_cnt_offset + PkgLayout::header_size > length) {
        return Pkg{*type, std::nullopt, {}, std::move(fih)};
    }

    PkgHeader header = read_header(stream, fih.embedded_cnt_offset);
    auto entries = read_entry_table(stream, header, fih.embedded_cnt_offset);
    resolve_names(stream, entries, fih.embedded_cnt_offset);
    return Pkg{*type, std::move(header), std::move(entries), std::move(fih)};
}

Pkg read_pkg(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open PS5 PKG file for reading: " + path.string());
    }
    return read_pkg(file);
}

std::vector<std::byte> write_cnt(const PkgWriterOptions& options)
{
    if (options.content_id.empty() || options.content_id.size() > PkgLayout::content_id_size) {
        throw std::invalid_argument("Content id is missing or exceeds the 0x30-byte CNT field.");
    }

    std::vector<PkgWriterEntry> entries = options.entries;
    bool has_name_table = std::any_of(entries.begin(), entries.end(), [](const PkgWriterEntry& entry) {
        return entry.id == static_cast<std::uint32_t>(EntryId::entry_names);
    });

    std::map<std::size_t, std::uint32_t> name_offsets;
    std::vector<std::byte> name_table{std::byte{0}};
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].name.empty()) {
            continue;
        }

        name_offsets[i] = static_cast<std::uint32_t>(name_table.size());
        for (char ch : entries[i].name) {
            if (static_cast<unsigned char>(ch) > 0x7F) {
                throw std::invalid_argument("CNT entry names must be ASCII.");
            }
            name_table.push_back(static_cast<std::byte>(ch));
        }
        name_table.push_back(std::byte{0});
    }

    if (!has_name_table) {
        entries.push_back(PkgWriterEntry{
            static_cast<std::uint32_t>(EntryId::entry_names),
            {},
            name_table,
            0,
            0});
    } else {
        auto it = std::find_if(entries.begin(), entries.end(), [](const PkgWriterEntry& entry) {
            return entry.id == static_cast<std::uint32_t>(EntryId::entry_names);
        });
        it->data = name_table;
    }

    if (entries.size() > 0x10000u) {
        throw std::invalid_argument("CNT writer entry count is too large.");
    }

    const auto entry_count = static_cast<std::uint32_t>(entries.size());
    const auto entry_table_offset = static_cast<std::uint32_t>(PkgLayout::header_size);
    const auto data_start = static_cast<std::uint32_t>(
        PkgLayout::header_size + entry_count * PkgLayout::entry_meta_size);

    std::vector<std::uint32_t> data_offsets(entry_count);
    std::uint32_t cursor = data_start;
    for (std::uint32_t i = 0; i < entry_count; ++i) {
        if (entries[i].data.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::invalid_argument("CNT entry payload is too large.");
        }
        cursor = align_up(cursor, 16);
        data_offsets[i] = cursor;
        cursor += static_cast<std::uint32_t>(entries[i].data.size());
    }
    const std::uint32_t body_end = align_up(cursor, 16);

    std::vector<std::byte> image(body_end);
    std::copy(PkgLayout::cnt_magic.begin(), PkgLayout::cnt_magic.end(), image.begin());

    write_be32(image.data() + 0x04, options.flags);
    write_be32(image.data() + 0x10, entry_count);
    write_be16(image.data() + 0x14, options.sc_entry_count);
    write_be32(image.data() + 0x18, entry_table_offset);
    write_be64(image.data() + 0x20, data_start);
    write_be64(image.data() + 0x28, body_end - data_start);
    write_ascii(image, 0x40, options.content_id);
    write_be32(image.data() + 0x70, options.drm_type);
    write_be32(image.data() + 0x74, options.content_type);

    for (std::uint32_t i = 0; i < entry_count; ++i) {
        const PkgWriterEntry& entry = entries[i];
        const std::uint32_t record_offset =
            entry_table_offset + i * static_cast<std::uint32_t>(PkgLayout::entry_meta_size);

        write_be32(image.data() + record_offset, entry.id);
        write_be32(
            image.data() + record_offset + 0x04,
            name_offsets.count(i) != 0 ? name_offsets[i] : 0);
        write_be32(image.data() + record_offset + 0x08, entry.flags1);
        write_be32(image.data() + record_offset + 0x0C, entry.flags2);
        write_be32(image.data() + record_offset + 0x10, data_offsets[i]);
        write_be32(
            image.data() + record_offset + 0x14,
            static_cast<std::uint32_t>(entry.data.size()));

        if (!entry.data.empty()) {
            std::copy(entry.data.begin(), entry.data.end(), image.begin() + data_offsets[i]);
        }
    }

    return image;
}

void write_cnt_file(const PkgWriterOptions& options, const std::filesystem::path& path)
{
    const auto image = write_cnt(options);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not open CNT output path for writing: " + path.string());
    }
    file.write(reinterpret_cast<const char*>(image.data()), static_cast<std::streamsize>(image.size()));
    if (!file) {
        throw std::runtime_error("Failed to write CNT output path: " + path.string());
    }
}

} // namespace prosperopkg
