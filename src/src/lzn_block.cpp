// LibProsperoPkg - A library for building and inspecting PS5 packages.
// Copyright (C) 2026 seregonwar.

#include <prosperopkg/lzn_block.hpp>

#include <prosperopkg/crc32c.hpp>
#include <prosperopkg/lzn.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace prosperopkg {
namespace {

constexpr std::array<std::byte, 4> block_magic{
    std::byte{'L'}, std::byte{'Z'}, std::byte{'N'}, std::byte{'B'}};
constexpr std::uint16_t block_version = 1;
constexpr std::size_t block_header_size = 64;
constexpr std::size_t block_entry_size = 24;
constexpr std::uint16_t block_entry_raw = 1;
constexpr std::uint32_t min_block_size = 16u * 1024u;
constexpr std::uint32_t max_block_size = 4u * 1024u * 1024u;

[[nodiscard]] bool is_power_of_two(std::uint32_t value) noexcept
{
    return value != 0 && (value & (value - 1u)) == 0;
}

[[nodiscard]] std::uint16_t read_le16(std::span<const std::byte> data, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[offset]) |
        (static_cast<std::uint16_t>(data[offset + 1]) << 8u));
}

[[nodiscard]] std::uint32_t read_le32(std::span<const std::byte> data, std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

[[nodiscard]] std::uint64_t read_le64(std::span<const std::byte> data, std::size_t offset) noexcept
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8u);
    }
    return value;
}

void write_le16(std::span<std::byte> data, std::size_t offset, std::uint16_t value) noexcept
{
    data[offset + 0] = static_cast<std::byte>(value & 0xFFu);
    data[offset + 1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
}

void write_le32(std::span<std::byte> data, std::size_t offset, std::uint32_t value) noexcept
{
    for (std::size_t index = 0; index < 4; ++index) {
        data[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xFFu);
    }
}

void write_le64(std::span<std::byte> data, std::size_t offset, std::uint64_t value) noexcept
{
    for (std::size_t index = 0; index < 8; ++index) {
        data[offset + index] = static_cast<std::byte>((value >> (index * 8u)) & 0xFFu);
    }
}

void append_le16(std::vector<std::byte>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
}

void append_le32(std::vector<std::byte>& out, std::uint32_t value)
{
    for (std::size_t index = 0; index < 4; ++index) {
        out.push_back(static_cast<std::byte>((value >> (index * 8u)) & 0xFFu));
    }
}

void append_le64(std::vector<std::byte>& out, std::uint64_t value)
{
    for (std::size_t index = 0; index < 8; ++index) {
        out.push_back(static_cast<std::byte>((value >> (index * 8u)) & 0xFFu));
    }
}

void validate_options(const LznBlockOptions& options)
{
    if (options.codec != LznBlockCodec::store && options.codec != LznBlockCodec::lzn1) {
        throw std::invalid_argument("Unsupported LZN block codec.");
    }
    if (!is_power_of_two(options.block_size) ||
        options.block_size < min_block_size ||
        options.block_size > max_block_size) {
        throw std::invalid_argument("LZN block size must be a power of two between 16 KiB and 4 MiB.");
    }
}

[[nodiscard]] std::uint32_t block_count_for(std::uint64_t size, std::uint32_t block_size)
{
    if (size == 0) {
        return 0;
    }
    const std::uint64_t count = (size + block_size - 1u) / block_size;
    if (count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("LZN block archive has too many blocks.");
    }
    return static_cast<std::uint32_t>(count);
}

[[nodiscard]] std::vector<std::byte> encode_header(
    const LznBlockOptions& options,
    std::uint32_t block_count,
    std::uint64_t original_size,
    std::uint64_t archive_size,
    std::uint64_t index_offset,
    std::uint32_t index_size,
    std::uint32_t payload_crc)
{
    std::vector<std::byte> header(block_header_size);
    std::copy(block_magic.begin(), block_magic.end(), header.begin());
    write_le16(header, 0x04, block_version);
    write_le16(header, 0x06, 0);
    write_le16(header, 0x08, static_cast<std::uint16_t>(options.codec));
    write_le16(header, 0x0A, 0);
    write_le32(header, 0x0C, options.block_size);
    write_le64(header, 0x10, original_size);
    write_le64(header, 0x18, archive_size);
    write_le64(header, 0x20, index_offset);
    write_le32(header, 0x28, block_count);
    write_le32(header, 0x2C, index_size);
    write_le32(header, 0x34, payload_crc);
    write_le32(header, 0x30, crc32c(std::span<const std::byte>(header).first(0x30)));
    return header;
}

[[nodiscard]] std::vector<std::byte> encode_entries(std::span<const LznBlockEntry> entries)
{
    std::vector<std::byte> out;
    out.reserve(entries.size() * block_entry_size);
    for (const auto& entry : entries) {
        append_le64(out, entry.offset);
        append_le32(out, entry.encoded_size);
        append_le32(out, entry.decoded_size);
        append_le32(out, entry.checksum);
        append_le16(out, entry.flags);
        append_le16(out, 0);
    }
    return out;
}

void validate_info(const LznBlockInfo& info, std::span<const std::byte> data)
{
    if (info.version != block_version) {
        throw std::runtime_error("Unsupported LZN block archive version.");
    }
    if (info.codec != LznBlockCodec::store && info.codec != LznBlockCodec::lzn1) {
        throw std::runtime_error("Unsupported LZN block archive codec.");
    }
    if (!is_power_of_two(info.block_size) ||
        info.block_size < min_block_size ||
        info.block_size > max_block_size) {
        throw std::runtime_error("Invalid LZN block size.");
    }
    if (info.archive_size != data.size()) {
        throw std::runtime_error("LZN block archive size does not match the header.");
    }
    if (info.index_size != info.block_count * block_entry_size) {
        throw std::runtime_error("LZN block archive index size is invalid.");
    }
    if (info.index_offset > data.size() || info.index_size > data.size() - info.index_offset) {
        throw std::runtime_error("LZN block archive index is outside the file.");
    }
    if (info.block_count != block_count_for(info.original_size, info.block_size)) {
        throw std::runtime_error("LZN block count does not match the declared size.");
    }
}

[[nodiscard]] LznBlockInfo read_info_unchecked(std::span<const std::byte> data)
{
    LznBlockInfo info;
    info.version = read_le16(data, 0x04);
    info.flags = read_le16(data, 0x06);
    info.codec = static_cast<LznBlockCodec>(read_le16(data, 0x08));
    info.block_size = read_le32(data, 0x0C);
    info.original_size = read_le64(data, 0x10);
    info.archive_size = read_le64(data, 0x18);
    info.index_offset = read_le64(data, 0x20);
    info.block_count = read_le32(data, 0x28);
    info.index_size = read_le32(data, 0x2C);
    return info;
}

void decode_block_to(
    std::span<const std::byte> archive,
    const LznBlockInfo& info,
    const LznBlockEntry& entry,
    std::span<std::byte> output)
{
    if (entry.encoded_size > archive.size() || entry.offset > archive.size() - entry.encoded_size) {
        throw std::runtime_error("LZN block entry points outside the archive.");
    }
    if (entry.offset < block_header_size || entry.offset + entry.encoded_size > info.index_offset) {
        throw std::runtime_error("LZN block entry overlaps archive metadata.");
    }
    if (output.size() < entry.decoded_size) {
        throw std::runtime_error("LZN block output buffer is too small.");
    }

    const auto encoded = archive.subspan(static_cast<std::size_t>(entry.offset), entry.encoded_size);
    if (entry.stored_raw()) {
        if (entry.encoded_size != entry.decoded_size) {
            throw std::runtime_error("Stored LZN block size mismatch.");
        }
        if (!encoded.empty()) {
            std::memcpy(output.data(), encoded.data(), encoded.size());
        }
    } else {
        const auto written = lzn_decompress_to(encoded, output.first(entry.decoded_size));
        if (written != entry.decoded_size) {
            throw std::runtime_error("LZN block decoded to the wrong size.");
        }
    }

    if (crc32c(output.first(entry.decoded_size)) != entry.checksum) {
        throw std::runtime_error("LZN block checksum mismatch.");
    }
}

} // namespace

bool is_lzn_block_archive(std::span<const std::byte> data) noexcept
{
    return data.size() >= block_header_size &&
           std::equal(block_magic.begin(), block_magic.end(), data.begin());
}

LznBlockInfo read_lzn_block_info(std::span<const std::byte> data)
{
    if (!is_lzn_block_archive(data)) {
        throw std::runtime_error("Input is not an LZN block archive.");
    }
    const auto expected_crc = read_le32(data, 0x30);
    if (crc32c(data.first(0x30)) != expected_crc) {
        throw std::runtime_error("LZN block header checksum mismatch.");
    }
    auto info = read_info_unchecked(data);
    validate_info(info, data);
    return info;
}

std::vector<LznBlockEntry> read_lzn_block_entries(std::span<const std::byte> data)
{
    const auto info = read_lzn_block_info(data);
    std::vector<LznBlockEntry> entries;
    entries.reserve(info.block_count);
    const auto index = data.subspan(static_cast<std::size_t>(info.index_offset), info.index_size);
    for (std::uint32_t block = 0; block < info.block_count; ++block) {
        const std::size_t offset = block * block_entry_size;
        LznBlockEntry entry;
        entry.offset = read_le64(index, offset + 0x00);
        entry.encoded_size = read_le32(index, offset + 0x08);
        entry.decoded_size = read_le32(index, offset + 0x0C);
        entry.checksum = read_le32(index, offset + 0x10);
        entry.flags = read_le16(index, offset + 0x14);
        if ((entry.flags & ~block_entry_raw) != 0) {
            throw std::runtime_error("Unsupported LZN block entry flags.");
        }
        if (entry.decoded_size == 0 || entry.decoded_size > info.block_size) {
            throw std::runtime_error("Invalid LZN block decoded size.");
        }
        entries.push_back(entry);
    }
    return entries;
}

std::vector<std::byte> lzn_block_compress(std::span<const std::byte> input, const LznBlockOptions& options)
{
    validate_options(options);

    const std::uint32_t block_count = block_count_for(input.size(), options.block_size);
    const std::uint32_t index_size = block_count * static_cast<std::uint32_t>(block_entry_size);
    std::vector<LznBlockEntry> entries;
    entries.reserve(block_count);

    std::vector<std::byte> out(block_header_size);
    std::uint32_t payload_crc_state = 0xFFFFFFFFu;

    for (std::uint32_t block = 0; block < block_count; ++block) {
        const std::size_t offset = static_cast<std::size_t>(block) * options.block_size;
        const std::size_t size = std::min<std::size_t>(options.block_size, input.size() - offset);
        const auto plain = input.subspan(offset, size);

        std::vector<std::byte> encoded;
        bool stored_raw = true;
        if (options.codec == LznBlockCodec::lzn1) {
            encoded = lzn_compress(plain, options.level);
            stored_raw = read_lzn_frame_info(encoded).stored_raw() || encoded.size() >= plain.size();
        }

        LznBlockEntry entry;
        entry.offset = out.size();
        entry.decoded_size = static_cast<std::uint32_t>(plain.size());
        entry.checksum = crc32c(plain);
        if (stored_raw) {
            entry.encoded_size = static_cast<std::uint32_t>(plain.size());
            entry.flags = block_entry_raw;
            out.insert(out.end(), plain.begin(), plain.end());
            payload_crc_state = crc32c_update(payload_crc_state, plain);
        } else {
            entry.encoded_size = static_cast<std::uint32_t>(encoded.size());
            entry.flags = 0;
            out.insert(out.end(), encoded.begin(), encoded.end());
            payload_crc_state = crc32c_update(payload_crc_state, encoded);
        }
        entries.push_back(entry);
    }

    const std::uint64_t index_offset = out.size();
    const auto encoded_entries = encode_entries(entries);
    out.insert(out.end(), encoded_entries.begin(), encoded_entries.end());

    const auto header = encode_header(
        options,
        block_count,
        input.size(),
        out.size(),
        index_offset,
        index_size,
        ~payload_crc_state);
    std::copy(header.begin(), header.end(), out.begin());
    return out;
}

std::size_t lzn_block_decompress_to(std::span<const std::byte> archive, std::span<std::byte> output)
{
    const auto info = read_lzn_block_info(archive);
    if (info.original_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("LZN block archive is too large for this host.");
    }
    const auto original_size = static_cast<std::size_t>(info.original_size);
    if (output.size() < original_size) {
        throw std::runtime_error("LZN block output buffer is too small.");
    }

    const auto entries = read_lzn_block_entries(archive);
    std::size_t cursor = 0;
    for (const auto& entry : entries) {
        decode_block_to(archive, info, entry, output.subspan(cursor, entry.decoded_size));
        cursor += entry.decoded_size;
    }
    return cursor;
}

std::vector<std::byte> lzn_block_decompress(std::span<const std::byte> archive)
{
    const auto info = read_lzn_block_info(archive);
    if (info.original_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("LZN block archive is too large for this host.");
    }
    std::vector<std::byte> out(static_cast<std::size_t>(info.original_size));
    (void)lzn_block_decompress_to(archive, out);
    return out;
}

std::vector<std::byte> lzn_block_decompress_range(
    std::span<const std::byte> archive,
    std::uint64_t offset,
    std::size_t size)
{
    const auto info = read_lzn_block_info(archive);
    if (offset > info.original_size) {
        throw std::runtime_error("LZN block range offset is outside the archive.");
    }
    const std::uint64_t available = info.original_size - offset;
    if (size > available) {
        size = static_cast<std::size_t>(available);
    }
    if (size == 0) {
        return {};
    }

    const auto entries = read_lzn_block_entries(archive);
    std::vector<std::byte> out(size);
    std::vector<std::byte> scratch(info.block_size);

    std::size_t written = 0;
    std::uint64_t current = offset;
    while (written < size) {
        const std::uint32_t block = static_cast<std::uint32_t>(current / info.block_size);
        const std::size_t block_offset = static_cast<std::size_t>(current % info.block_size);
        const auto& entry = entries.at(block);
        decode_block_to(archive, info, entry, scratch);

        const std::size_t readable = std::min<std::size_t>(entry.decoded_size - block_offset, size - written);
        std::copy_n(
            scratch.begin() + static_cast<std::ptrdiff_t>(block_offset),
            readable,
            out.begin() + static_cast<std::ptrdiff_t>(written));
        written += readable;
        current += readable;
    }
    return out;
}

} // namespace prosperopkg
