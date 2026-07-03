// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/pfsc.hpp>

#include <prosperopkg/hash.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

#ifdef PROSPEROPKG_HAS_ZLIB
#include <zlib.h>
#endif

namespace prosperopkg {
namespace {

constexpr std::array<char, 4> pfsc_magic{'P', 'F', 'S', 'C'};
constexpr std::uint32_t default_block_size = 0x10000;
constexpr std::uint64_t block_offsets_offset = 0x400;
constexpr std::uint64_t initial_data_offset = 0x10000;
constexpr std::uint64_t offset_entry_size = 8;
constexpr std::uint16_t pfs_v3_version = 3;
constexpr std::uint16_t pfs_v3_section_count = 7;
constexpr std::uint32_t pfs_v3_default_block_size = 0x40000;
constexpr std::uint32_t pfs_v3_magic = 0x43534650;
constexpr std::uint32_t pfs_v3_encode_param_0c = 0x0802;
constexpr std::size_t pfs_v3_header_size = 0x48;
constexpr std::size_t pfs_v3_directory_entry_size = 16;
constexpr std::size_t pfs_v3_section_alignment = 8;
constexpr std::size_t pfs_v3_data_alignment = 0x400;
constexpr std::uint64_t pfs_v3_stored_flag_base = 0x0C;
constexpr std::uint64_t pfs_v3_stored_flag_large_half = 0xC0;
constexpr std::uint64_t pfs_v3_boundary_flag_shift = 48;
constexpr std::uint64_t pfs_v3_size_hint_shift = 44;
constexpr std::uint64_t pfs_v3_size_hint_max = 0x1FFFF;
constexpr std::uint64_t pfs_v3_boundary_offset_mask = 0xFFFFFFFFFFFu;

constexpr std::array<std::byte, 20> pfs_v3_git_hash{
    std::byte{0x23}, std::byte{0x98}, std::byte{0x7D}, std::byte{0x16}, std::byte{0xC9},
    std::byte{0x20}, std::byte{0x9A}, std::byte{0xC7}, std::byte{0x28}, std::byte{0x37},
    std::byte{0x19}, std::byte{0x32}, std::byte{0x7E}, std::byte{0x0F}, std::byte{0x50},
    std::byte{0x6B}, std::byte{0xBC}, std::byte{0xF4}, std::byte{0x59}, std::byte{0xF4}};

constexpr std::array<std::byte, 64> pfs_v3_shuffle_table{
    std::byte{0x04}, std::byte{0x04}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x02}, std::byte{0x02}, std::byte{0x04}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x01}, std::byte{0x01}, std::byte{0x06}, std::byte{0x00},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x01}, std::byte{0x01}, std::byte{0x01}, std::byte{0x01},
    std::byte{0x01}, std::byte{0x01}, std::byte{0x01}, std::byte{0x01},
    std::byte{0x08}, std::byte{0x02}, std::byte{0x02}, std::byte{0x04},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x01}, std::byte{0x01}, std::byte{0x06}, std::byte{0x02},
    std::byte{0x02}, std::byte{0x04}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x01}, std::byte{0x01}, std::byte{0x06}, std::byte{0x01},
    std::byte{0x01}, std::byte{0x06}, std::byte{0x00}, std::byte{0x00},
    std::byte{0x04}, std::byte{0x04}, std::byte{0x04}, std::byte{0x04},
    std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};

struct PfsV3Section {
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    bool present = false;
};

struct PfsV3Info {
    std::uint32_t block_size = 0;
    std::uint64_t uncompressed_size = 0;
    std::uint64_t total_size = 0;
    std::uint64_t block_count = 0;
    std::uint64_t data_offset = 0;
    std::array<PfsV3Section, 8> sections{};
    std::vector<std::byte> boundaries;
    std::vector<std::byte> hashes;
};

[[nodiscard]] bool valid_block_size(std::uint32_t block_size) noexcept
{
    return block_size >= 0x1000u &&
           block_size <= 0x200000u &&
           (block_size & (block_size - 1u)) == 0u;
}

[[nodiscard]] std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) noexcept
{
    return ((value + alignment - 1u) / alignment) * alignment;
}

[[nodiscard]] std::uint64_t header_size_for(std::uint64_t block_count, std::uint32_t block_size)
{
    const std::uint64_t pointer_table = (block_count + 1u) * offset_entry_size;
    const std::uint64_t capacity = initial_data_offset - block_offsets_offset;
    const std::uint64_t extra = pointer_table > capacity ? pointer_table - capacity : 0;
    const std::uint64_t extra_blocks = extra == 0 ? 0 : align_up(extra, block_size) / block_size;
    return initial_data_offset + extra_blocks * block_size;
}

void write_le32(std::ostream& out, std::uint32_t value)
{
    const std::array<char, 4> bytes{
        static_cast<char>(value & 0xFFu),
        static_cast<char>((value >> 8u) & 0xFFu),
        static_cast<char>((value >> 16u) & 0xFFu),
        static_cast<char>((value >> 24u) & 0xFFu),
    };
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_le64(std::ostream& out, std::uint64_t value)
{
    std::array<char, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<char>((value >> (i * 8u)) & 0xFFu);
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void write_le16(std::span<std::byte> data, std::size_t offset, std::uint16_t value)
{
    data[offset] = static_cast<std::byte>(value & 0xFFu);
    data[offset + 1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
}

void write_le32(std::span<std::byte> data, std::size_t offset, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        data[offset + static_cast<std::size_t>(i)] = static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
    }
}

void write_le64(std::span<std::byte> data, std::size_t offset, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        data[offset + static_cast<std::size_t>(i)] = static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
    }
}

[[nodiscard]] std::uint16_t read_le16(std::span<const std::byte> header, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(header[offset]) |
        (static_cast<std::uint16_t>(header[offset + 1]) << 8u));
}

[[nodiscard]] std::uint32_t read_le32(std::span<const std::byte> header, std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(header[offset]) |
           (static_cast<std::uint32_t>(header[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(header[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(header[offset + 3]) << 24u);
}

[[nodiscard]] std::uint64_t read_le64(std::span<const std::byte> header, std::size_t offset) noexcept
{
    return static_cast<std::uint64_t>(read_le32(header, offset)) |
           (static_cast<std::uint64_t>(read_le32(header, offset + 4)) << 32u);
}

[[nodiscard]] std::uint64_t read_le64_at(std::istream& in, std::uint64_t offset)
{
    std::array<unsigned char, 8> bytes{};
    in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (in.gcount() != static_cast<std::streamsize>(bytes.size())) {
        throw std::runtime_error("PFSC offset table is truncated.");
    }

    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8u);
    }
    return value;
}

[[nodiscard]] std::uint64_t file_size(std::ifstream& file)
{
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        throw std::runtime_error("Could not determine file size.");
    }
    file.seekg(0, std::ios::beg);
    return static_cast<std::uint64_t>(end);
}

void ensure_parent(const std::filesystem::path& path)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

[[nodiscard]] bool is_pfs_v3_header(std::span<const std::byte> header) noexcept
{
    return header.size() >= pfs_v3_header_size &&
           read_le32(header, 0x00) == pfs_v3_magic &&
           (read_le16(header, 0x04) == 2u || read_le16(header, 0x04) == pfs_v3_version);
}

[[nodiscard]] std::vector<std::byte> read_file_bytes(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file: " + path.string());
    }

    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        throw std::runtime_error("Could not determine file size: " + path.string());
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (file.gcount() != static_cast<std::streamsize>(data.size())) {
            throw std::runtime_error("Could not read file: " + path.string());
        }
    }
    return data;
}

void write_directory_entry(
    std::span<std::byte> data,
    std::size_t index,
    std::uint16_t id,
    std::uint32_t offset,
    std::uint32_t size)
{
    const std::size_t base = pfs_v3_header_size + index * pfs_v3_directory_entry_size;
    write_le16(data, base + 0x00, id);
    write_le32(data, base + 0x02, offset);
    write_le32(data, base + 0x0A, size);
}

[[nodiscard]] std::array<std::byte, 32> pfs_v3_file_digest(
    std::span<const std::byte> header,
    std::span<const std::byte> shuffle,
    std::span<const std::byte> boundaries,
    std::span<const std::byte> hashes)
{
    std::array<std::byte, 32> header32{};
    write_le32(header32, 0, 1);
    std::copy_n(header.begin() + 0x08, 8, header32.begin() + 4);
    std::copy_n(header.begin() + 0x10, 16, header32.begin() + 16);

    std::vector<std::byte> preimage;
    preimage.reserve(header32.size() + shuffle.size() + boundaries.size() + hashes.size());
    preimage.insert(preimage.end(), header32.begin(), header32.end());
    preimage.insert(preimage.end(), shuffle.begin(), shuffle.end());
    preimage.insert(preimage.end(), boundaries.begin(), boundaries.end());
    preimage.insert(preimage.end(), hashes.begin(), hashes.end());
    return sha3_256(preimage);
}

[[nodiscard]] PfsV3Info parse_pfs_v3(std::istream& in, std::uint64_t stream_size)
{
    std::array<std::byte, pfs_v3_header_size> header{};
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (in.gcount() != static_cast<std::streamsize>(header.size())) {
        throw std::runtime_error("PFSv3 PFSC header is truncated.");
    }
    if (!is_pfs_v3_header(header)) {
        throw std::runtime_error("Input is not a PFSv2/PFSv3 PFSC image.");
    }

    const std::uint16_t section_count = read_le16(header, 0x06);
    if (section_count == 0 || section_count > 64) {
        throw std::runtime_error("PFSv3 PFSC section count is out of range.");
    }
    const std::uint64_t directory_size =
        pfs_v3_header_size + static_cast<std::uint64_t>(section_count) * pfs_v3_directory_entry_size;
    if (directory_size > stream_size || directory_size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("PFSv3 PFSC section directory is out of range.");
    }

    std::vector<std::byte> directory(static_cast<std::size_t>(directory_size));
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(directory.data()), static_cast<std::streamsize>(directory.size()));
    if (in.gcount() != static_cast<std::streamsize>(directory.size())) {
        throw std::runtime_error("PFSv3 PFSC section directory is truncated.");
    }

    PfsV3Info info;
    info.block_size = read_le32(directory, 0x08);
    info.uncompressed_size = read_le64(directory, 0x18);
    info.total_size = read_le64(directory, 0x20);
    if (!valid_block_size(info.block_size)) {
        throw std::runtime_error("Unsupported PFSv3 PFSC block size.");
    }
    if (info.total_size > stream_size) {
        throw std::runtime_error("PFSv3 PFSC file is truncated.");
    }

    for (std::uint16_t index = 0; index < section_count; ++index) {
        const std::size_t entry = pfs_v3_header_size + static_cast<std::size_t>(index) * pfs_v3_directory_entry_size;
        const std::uint16_t id = read_le16(directory, entry);
        if (id == 0) {
            continue;
        }
        const std::uint64_t offset = read_le32(directory, entry + 0x02);
        const std::uint64_t size = read_le32(directory, entry + 0x0A);
        if (offset + size > info.total_size) {
            throw std::runtime_error("PFSv3 PFSC section points outside the file.");
        }
        if (id < info.sections.size()) {
            info.sections[id] = PfsV3Section{offset, size, true};
        }
    }

    const auto& boundaries = info.sections[3];
    const auto& data = info.sections[7];
    if (!boundaries.present || boundaries.size < pfs_v3_directory_entry_size * 2u ||
        (boundaries.size % pfs_v3_directory_entry_size) != 0) {
        throw std::runtime_error("PFSv3 PFSC boundary table is malformed.");
    }
    if (!data.present) {
        throw std::runtime_error("PFSv3 PFSC data section is missing.");
    }
    info.block_count = boundaries.size / pfs_v3_directory_entry_size - 1u;
    info.data_offset = data.offset;

    info.boundaries.resize(static_cast<std::size_t>(boundaries.size));
    in.seekg(static_cast<std::streamoff>(boundaries.offset), std::ios::beg);
    in.read(reinterpret_cast<char*>(info.boundaries.data()), static_cast<std::streamsize>(info.boundaries.size()));
    if (in.gcount() != static_cast<std::streamsize>(info.boundaries.size())) {
        throw std::runtime_error("PFSv3 PFSC boundary table is truncated.");
    }

    const auto& hashes = info.sections[4];
    if (hashes.present && hashes.size >= info.block_count * 32u) {
        info.hashes.resize(static_cast<std::size_t>(hashes.size));
        in.seekg(static_cast<std::streamoff>(hashes.offset), std::ios::beg);
        in.read(reinterpret_cast<char*>(info.hashes.data()), static_cast<std::streamsize>(info.hashes.size()));
        if (in.gcount() != static_cast<std::streamsize>(info.hashes.size())) {
            throw std::runtime_error("PFSv3 PFSC hash table is truncated.");
        }
    }

    return info;
}

[[nodiscard]] std::vector<char> inflate_legacy_zlib_block(
    std::istream& input,
    std::uint64_t begin,
    std::uint64_t stored_size,
    std::uint32_t expected_size)
{
    if (stored_size > static_cast<std::uint64_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("PFSC compressed block is too large.");
    }

    std::vector<unsigned char> compressed(static_cast<std::size_t>(stored_size));
    input.seekg(static_cast<std::streamoff>(begin), std::ios::beg);
    input.read(reinterpret_cast<char*>(compressed.data()), static_cast<std::streamsize>(compressed.size()));
    if (input.gcount() != static_cast<std::streamsize>(compressed.size())) {
        throw std::runtime_error("PFSC compressed block is truncated.");
    }

#ifdef PROSPEROPKG_HAS_ZLIB
    std::vector<char> output(expected_size);
    uLongf output_size = static_cast<uLongf>(output.size());
    const int rc = uncompress(
        reinterpret_cast<Bytef*>(output.data()),
        &output_size,
        reinterpret_cast<const Bytef*>(compressed.data()),
        static_cast<uLong>(compressed.size()));
    if (rc != Z_OK || output_size != expected_size) {
        throw std::runtime_error("PFSC zlib block could not be decompressed.");
    }
    return output;
#else
    (void)expected_size;
    throw std::runtime_error("PFSC zlib block support was not compiled into this build.");
#endif
}

[[nodiscard]] PfscInfo parse_header(std::istream& in)
{
    std::array<std::byte, 0x30> header{};
    in.seekg(0, std::ios::beg);
    in.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (in.gcount() != static_cast<std::streamsize>(header.size())) {
        throw std::runtime_error("PFSC header is truncated.");
    }

    if (header[0] != std::byte{'P'} ||
        header[1] != std::byte{'F'} ||
        header[2] != std::byte{'S'} ||
        header[3] != std::byte{'C'}) {
        throw std::runtime_error("Input is not a PFSC image.");
    }

    if (read_le32(header, 0x04) != 0) {
        throw std::runtime_error("Unsupported PFSC header value at 0x04.");
    }

    const std::uint32_t block_size = read_le32(header, 0x0C);
    const std::uint64_t block_size_64 = read_le64(header, 0x10);
    const std::uint64_t block_offsets = read_le64(header, 0x18);
    const std::uint64_t data_start = read_le64(header, 0x20);
    const std::uint64_t data_length = read_le64(header, 0x28);

    if (!valid_block_size(block_size) || block_size_64 != block_size) {
        throw std::runtime_error("Unsupported PFSC block size.");
    }
    if (block_offsets != block_offsets_offset) {
        throw std::runtime_error("Unsupported PFSC offset table location.");
    }
    if (data_length % block_size != 0) {
        throw std::runtime_error("PFSC data length is not block-aligned.");
    }

    return PfscInfo{
        block_size,
        data_length,
        data_length / block_size,
        data_start};
}

} // namespace

bool is_pfsc_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    std::array<char, 4> magic{};
    file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    return file.gcount() == static_cast<std::streamsize>(magic.size()) && magic == pfsc_magic;
}

PfscInfo read_pfsc_info(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open PFSC file: " + path.string());
    }

    const std::uint64_t size = file_size(file);
    std::array<std::byte, pfs_v3_header_size> header{};
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (file.gcount() == static_cast<std::streamsize>(header.size()) && is_pfs_v3_header(header)) {
        const auto info = parse_pfs_v3(file, size);
        return PfscInfo{
            info.block_size,
            info.uncompressed_size,
            info.block_count,
            info.data_offset};
    }

    file.clear();
    file.seekg(0, std::ios::beg);
    return parse_header(file);
}

void pack_pfsc_raw(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    std::uint32_t block_size)
{
    if (block_size == 0) {
        block_size = default_block_size;
    }
    if (!valid_block_size(block_size)) {
        throw std::runtime_error("PFSC block size must be a power of two between 4096 and 2097152.");
    }

    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open PFSC input file: " + input_path.string());
    }

    const std::uint64_t raw_size = file_size(input);
    const std::uint64_t block_count = raw_size == 0 ? 0 : (raw_size + block_size - 1u) / block_size;
    const std::uint64_t header_size = header_size_for(block_count, block_size);
    const std::uint64_t data_length = block_count * static_cast<std::uint64_t>(block_size);

    ensure_parent(output_path);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create PFSC output file: " + output_path.string());
    }

    output.write(pfsc_magic.data(), static_cast<std::streamsize>(pfsc_magic.size()));
    write_le32(output, 0);
    write_le32(output, 6);
    write_le32(output, block_size);
    write_le64(output, block_size);
    write_le64(output, block_offsets_offset);
    write_le64(output, header_size);
    write_le64(output, data_length);

    output.seekp(static_cast<std::streamoff>(block_offsets_offset), std::ios::beg);
    for (std::uint64_t i = 0; i <= block_count; ++i) {
        write_le64(output, header_size + i * static_cast<std::uint64_t>(block_size));
    }

    output.seekp(static_cast<std::streamoff>(header_size), std::ios::beg);
    std::vector<char> buffer(std::min<std::uint64_t>(block_size, 1u << 20u));
    std::uint64_t remaining = raw_size;
    while (remaining > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
        input.read(buffer.data(), chunk);
        if (input.gcount() != chunk) {
            throw std::runtime_error("Could not read PFSC input payload.");
        }
        output.write(buffer.data(), chunk);
        remaining -= static_cast<std::uint64_t>(chunk);
    }

    if (data_length > raw_size) {
        std::vector<char> zeros(std::min<std::uint64_t>(data_length - raw_size, 1u << 20u), 0);
        std::uint64_t padding = data_length - raw_size;
        while (padding > 0) {
            const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(padding, zeros.size()));
            output.write(zeros.data(), chunk);
            padding -= static_cast<std::uint64_t>(chunk);
        }
    }

    if (!output) {
        throw std::runtime_error("Could not write PFSC output file.");
    }
}

void pack_pfsc_zlib(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    int level,
    std::uint32_t block_size)
{
    if (block_size == 0) {
        block_size = default_block_size;
    }
    if (!valid_block_size(block_size)) {
        throw std::runtime_error("PFSC block size must be a power of two between 4096 and 2097152.");
    }

#ifndef PROSPEROPKG_HAS_ZLIB
    (void)level;
    pack_pfsc_raw(input_path, output_path, block_size);
#else
    const int zlib_level = std::clamp(level, 0, 9);
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open PFSC input file: " + input_path.string());
    }

    const std::uint64_t raw_size = file_size(input);
    const std::uint64_t block_count = raw_size == 0 ? 0 : (raw_size + block_size - 1u) / block_size;
    const std::uint64_t header_size = header_size_for(block_count, block_size);
    const std::uint64_t data_length = block_count * static_cast<std::uint64_t>(block_size);

    ensure_parent(output_path);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create PFSC output file: " + output_path.string());
    }

    output.seekp(static_cast<std::streamoff>(header_size), std::ios::beg);
    std::vector<std::uint64_t> offsets;
    offsets.reserve(static_cast<std::size_t>(block_count + 1u));
    offsets.push_back(header_size);

    std::vector<unsigned char> block(block_size);
    std::vector<unsigned char> compressed(compressBound(block_size));
    std::uint64_t remaining = raw_size;
    std::uint64_t compressed_blocks = 0;
    for (std::uint64_t index = 0; index < block_count; ++index) {
        const auto got = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, block_size));
        std::fill(block.begin(), block.end(), 0);
        if (got > 0) {
            input.read(reinterpret_cast<char*>(block.data()), got);
            if (input.gcount() != got) {
                throw std::runtime_error("Could not read PFSC input payload.");
            }
            remaining -= static_cast<std::uint64_t>(got);
        }

        uLongf compressed_size = static_cast<uLongf>(compressed.size());
        const int rc = compress2(
            compressed.data(),
            &compressed_size,
            block.data(),
            static_cast<uLong>(block.size()),
            zlib_level);
        const bool use_compressed = rc == Z_OK && compressed_size < block_size;
        if (use_compressed) {
            output.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressed_size));
            offsets.push_back(offsets.back() + compressed_size);
            ++compressed_blocks;
        } else {
            output.write(reinterpret_cast<const char*>(block.data()), static_cast<std::streamsize>(block.size()));
            offsets.push_back(offsets.back() + block.size());
        }
    }

    output.seekp(0, std::ios::beg);
    output.write(pfsc_magic.data(), static_cast<std::streamsize>(pfsc_magic.size()));
    write_le32(output, 0);
    write_le32(output, 6);
    write_le32(output, block_size);
    write_le64(output, block_size);
    write_le64(output, block_offsets_offset);
    write_le64(output, header_size);
    write_le64(output, data_length);

    output.seekp(static_cast<std::streamoff>(block_offsets_offset), std::ios::beg);
    for (std::uint64_t offset : offsets) {
        write_le64(output, offset);
    }

    (void)compressed_blocks;
    if (!output) {
        throw std::runtime_error("Could not write PFSC output file.");
    }
#endif
}

std::vector<std::byte> pack_pfsc_pfs_v3_stored(
    std::span<const std::byte> payload,
    int level,
    std::uint32_t block_size)
{
    if (block_size == 0) {
        block_size = pfs_v3_default_block_size;
    }
    if (!valid_block_size(block_size)) {
        throw std::runtime_error("PFSv3 PFSC block size must be a power of two between 4096 and 2097152.");
    }

    const std::uint64_t block_count =
        payload.empty() ? 1u : (payload.size() + static_cast<std::uint64_t>(block_size) - 1u) / block_size;
    const std::uint64_t sec1_size = pfs_v3_git_hash.size();
    const std::uint64_t sec2_size = pfs_v3_shuffle_table.size();
    const std::uint64_t sec3_size = (block_count + 1u) * pfs_v3_directory_entry_size;
    const std::uint64_t sec4_size = block_count * 32u;
    const std::uint64_t sec5_size = block_count * pfs_v3_directory_entry_size;

    const std::uint64_t off1 = pfs_v3_header_size + pfs_v3_section_count * pfs_v3_directory_entry_size;
    const std::uint64_t off2 = align_up(off1 + sec1_size, pfs_v3_section_alignment);
    const std::uint64_t off3 = align_up(off2 + sec2_size, pfs_v3_section_alignment);
    const std::uint64_t off4 = align_up(off3 + sec3_size, pfs_v3_section_alignment);
    const std::uint64_t off5 = align_up(off4 + sec4_size, pfs_v3_section_alignment);
    const std::uint64_t off6 = align_up(off5 + sec5_size, pfs_v3_section_alignment);
    const std::uint64_t off7 = align_up(off6, pfs_v3_data_alignment);
    const std::uint64_t total_size = off7 + payload.size();
    if (total_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("PFSv3 PFSC output is too large for this host.");
    }

    std::vector<std::byte> out(static_cast<std::size_t>(total_size));
    write_le32(out, 0x00, pfs_v3_magic);
    write_le16(out, 0x04, pfs_v3_version);
    write_le16(out, 0x06, pfs_v3_section_count);
    write_le32(out, 0x08, block_size);
    write_le32(out, 0x0C, pfs_v3_encode_param_0c);
    const std::uint64_t encode_param_10 =
        2u |
        (static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(level))) << 8u) |
        (18ull << 16u);
    write_le64(out, 0x10, encode_param_10);
    write_le64(out, 0x18, payload.size());
    write_le64(out, 0x20, total_size);

    write_directory_entry(out, 0, 1, static_cast<std::uint32_t>(off1), static_cast<std::uint32_t>(sec1_size));
    write_directory_entry(out, 1, 2, static_cast<std::uint32_t>(off2), static_cast<std::uint32_t>(sec2_size));
    write_directory_entry(out, 2, 3, static_cast<std::uint32_t>(off3), static_cast<std::uint32_t>(sec3_size));
    write_directory_entry(out, 3, 4, static_cast<std::uint32_t>(off4), static_cast<std::uint32_t>(sec4_size));
    write_directory_entry(out, 4, 5, static_cast<std::uint32_t>(off5), static_cast<std::uint32_t>(sec5_size));
    write_directory_entry(out, 5, 6, static_cast<std::uint32_t>(off6), 0);
    write_directory_entry(out, 6, 7, static_cast<std::uint32_t>(off7), static_cast<std::uint32_t>(payload.size()));

    std::copy(pfs_v3_git_hash.begin(), pfs_v3_git_hash.end(), out.begin() + static_cast<std::ptrdiff_t>(off1));
    std::copy(
        pfs_v3_shuffle_table.begin(),
        pfs_v3_shuffle_table.end(),
        out.begin() + static_cast<std::ptrdiff_t>(off2));

    std::uint64_t cumulative = 0;
    const std::uint64_t half_block = block_size / 2u;
    for (std::uint64_t index = 0; index < block_count; ++index) {
        const std::uint64_t size = payload.empty()
            ? 0u
            : std::min<std::uint64_t>(block_size, payload.size() - cumulative);
        const std::uint64_t flags = pfs_v3_stored_flag_base |
            (size > half_block ? pfs_v3_stored_flag_large_half : 0u);
        const std::uint64_t size_hint = std::min<std::uint64_t>(
            size == 0 ? 0u : size - 1u,
            pfs_v3_size_hint_max);

        const std::size_t boundary = static_cast<std::size_t>(off3 + index * pfs_v3_directory_entry_size);
        write_le64(out, boundary, cumulative | (flags << pfs_v3_boundary_flag_shift));
        write_le64(out, boundary + 8, cumulative | (size_hint << pfs_v3_size_hint_shift));

        const auto block = payload.subspan(static_cast<std::size_t>(cumulative), static_cast<std::size_t>(size));
        const auto digest = sha3_256(block);
        std::copy(
            digest.begin(),
            digest.end(),
            out.begin() + static_cast<std::ptrdiff_t>(off4 + index * digest.size()));
        if (size > 0) {
            std::copy(
                block.begin(),
                block.end(),
                out.begin() + static_cast<std::ptrdiff_t>(off7 + cumulative));
        }
        cumulative += size;
    }

    const std::size_t sentinel = static_cast<std::size_t>(off3 + block_count * pfs_v3_directory_entry_size);
    write_le64(out, sentinel, payload.size());
    write_le64(out, sentinel + 8, payload.size());

    const auto file_digest = pfs_v3_file_digest(
        std::span<const std::byte>(out).subspan(0, pfs_v3_header_size),
        std::span<const std::byte>(out).subspan(static_cast<std::size_t>(off2), static_cast<std::size_t>(sec2_size)),
        std::span<const std::byte>(out).subspan(static_cast<std::size_t>(off3), static_cast<std::size_t>(sec3_size)),
        std::span<const std::byte>(out).subspan(static_cast<std::size_t>(off4), static_cast<std::size_t>(sec4_size)));
    std::copy(file_digest.begin(), file_digest.end(), out.begin() + 0x28);

    return out;
}

void pack_pfsc_pfs_v3_stored(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path,
    int level,
    std::uint32_t block_size)
{
    const auto payload = read_file_bytes(input_path);
    const auto container = pack_pfsc_pfs_v3_stored(payload, level, block_size);
    ensure_parent(output_path);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create PFSv3 PFSC output file: " + output_path.string());
    }
    if (!container.empty()) {
        output.write(reinterpret_cast<const char*>(container.data()), static_cast<std::streamsize>(container.size()));
    }
    if (!output) {
        throw std::runtime_error("Could not write PFSv3 PFSC output file: " + output_path.string());
    }
}

std::uint64_t unpack_pfsc(
    const std::filesystem::path& input_path,
    const std::filesystem::path& output_path)
{
    std::ifstream input(input_path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open PFSC input file: " + input_path.string());
    }

    const std::uint64_t input_size = file_size(input);
    std::array<std::byte, pfs_v3_header_size> header{};
    input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (input.gcount() == static_cast<std::streamsize>(header.size()) && is_pfs_v3_header(header)) {
        const PfsV3Info info = parse_pfs_v3(input, input_size);
        ensure_parent(output_path);
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            throw std::runtime_error("Could not create PFSv3 PFSC output file: " + output_path.string());
        }

        std::uint64_t max_written = 0;
        for (std::uint64_t block = 0; block < info.block_count; ++block) {
            const std::size_t entry = static_cast<std::size_t>(block * pfs_v3_directory_entry_size);
            const std::uint64_t e0 = read_le64(info.boundaries, entry);
            const std::uint64_t e1 = read_le64(info.boundaries, entry + 8);
            const std::uint64_t next_e0 = read_le64(info.boundaries, entry + pfs_v3_directory_entry_size);
            const std::uint64_t next_e1 = read_le64(info.boundaries, entry + pfs_v3_directory_entry_size + 8);

            const std::uint64_t comp_rel = e0 & pfs_v3_boundary_offset_mask;
            const std::uint64_t uncomp_rel = e1 & pfs_v3_boundary_offset_mask;
            const std::uint64_t comp_next = next_e0 & pfs_v3_boundary_offset_mask;
            const std::uint64_t uncomp_next = next_e1 & pfs_v3_boundary_offset_mask;
            if (comp_next < comp_rel || uncomp_next < uncomp_rel) {
                throw std::runtime_error("PFSv3 PFSC boundary table is not monotonic.");
            }

            const std::uint64_t comp_size = comp_next - comp_rel;
            const std::uint64_t uncomp_size = uncomp_next - uncomp_rel;
            if (uncomp_rel + uncomp_size > info.uncompressed_size ||
                info.data_offset + comp_rel + comp_size > input_size) {
                throw std::runtime_error("PFSv3 PFSC block points outside its file.");
            }
            if (comp_size != uncomp_size) {
                throw std::runtime_error("PFSv3 PFSC block uses Kraken compression; stored blocks are supported.");
            }
            if (comp_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
                throw std::runtime_error("PFSv3 PFSC block is too large for this host.");
            }

            std::vector<std::byte> block_bytes(static_cast<std::size_t>(comp_size));
            if (!block_bytes.empty()) {
                input.seekg(static_cast<std::streamoff>(info.data_offset + comp_rel), std::ios::beg);
                input.read(
                    reinterpret_cast<char*>(block_bytes.data()),
                    static_cast<std::streamsize>(block_bytes.size()));
                if (input.gcount() != static_cast<std::streamsize>(block_bytes.size())) {
                    throw std::runtime_error("PFSv3 PFSC stored block is truncated.");
                }
            }

            if (info.hashes.size() >= (block + 1u) * 32u) {
                const auto digest = sha3_256(block_bytes);
                const auto expected = info.hashes.begin() + static_cast<std::ptrdiff_t>(block * 32u);
                if (!std::equal(digest.begin(), digest.end(), expected)) {
                    throw std::runtime_error("PFSv3 PFSC stored block digest mismatch.");
                }
            }

            if (!block_bytes.empty()) {
                output.seekp(static_cast<std::streamoff>(uncomp_rel), std::ios::beg);
                output.write(
                    reinterpret_cast<const char*>(block_bytes.data()),
                    static_cast<std::streamsize>(block_bytes.size()));
            }
            max_written = std::max(max_written, uncomp_rel + uncomp_size);
        }

        if (max_written != info.uncompressed_size) {
            throw std::runtime_error("PFSv3 PFSC output length does not match the header.");
        }
        if (!output) {
            throw std::runtime_error("Could not write PFSv3 PFSC output file.");
        }
        return info.uncompressed_size;
    }

    input.clear();
    input.seekg(0, std::ios::beg);
    const PfscInfo info = parse_header(input);
    ensure_parent(output_path);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create PFSC output file: " + output_path.string());
    }

    std::vector<char> buffer(std::min<std::uint64_t>(info.block_size, 1u << 20u));
    std::uint64_t written = 0;
    for (std::uint64_t block = 0; block < info.block_count; ++block) {
        const std::uint64_t begin = read_le64_at(input, block_offsets_offset + block * offset_entry_size);
        const std::uint64_t end = read_le64_at(input, block_offsets_offset + (block + 1u) * offset_entry_size);
        if (end < begin) {
            throw std::runtime_error("PFSC offset table is not monotonic.");
        }
        const std::uint64_t stored_size = end - begin;
        if (stored_size == info.block_size) {
            input.seekg(static_cast<std::streamoff>(begin), std::ios::beg);
            std::uint64_t remaining = info.block_size;
            while (remaining > 0) {
                const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
                input.read(buffer.data(), chunk);
                if (input.gcount() != chunk) {
                    throw std::runtime_error("PFSC raw block is truncated.");
                }
                output.write(buffer.data(), chunk);
                remaining -= static_cast<std::uint64_t>(chunk);
                written += static_cast<std::uint64_t>(chunk);
            }
        } else if (stored_size > info.block_size) {
            std::fill(buffer.begin(), buffer.end(), 0);
            std::uint64_t remaining = info.block_size;
            while (remaining > 0) {
                const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(remaining, buffer.size()));
                output.write(buffer.data(), chunk);
                remaining -= static_cast<std::uint64_t>(chunk);
                written += static_cast<std::uint64_t>(chunk);
            }
        } else {
            const auto inflated = inflate_legacy_zlib_block(input, begin, stored_size, info.block_size);
            output.write(inflated.data(), static_cast<std::streamsize>(inflated.size()));
            written += inflated.size();
        }
    }

    if (!output) {
        throw std::runtime_error("Could not write PFSC output file.");
    }
    return written;
}

} // namespace prosperopkg
