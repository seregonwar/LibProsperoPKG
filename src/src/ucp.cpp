// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/ucp.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>

namespace prosperopkg {
namespace {

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

[[nodiscard]] std::size_t align_up_strict(std::size_t value)
{
    return ((value / UcpLayout::blob_alignment) + 1u) * UcpLayout::blob_alignment;
}

[[nodiscard]] bool less_by_unsigned_bytes(const std::string& a, const std::string& b)
{
    return std::lexicographical_compare(
        a.begin(),
        a.end(),
        b.begin(),
        b.end(),
        [](char lhs, char rhs) {
            return static_cast<unsigned char>(lhs) < static_cast<unsigned char>(rhs);
        });
}

[[nodiscard]] std::uint32_t rotl(std::uint32_t value, int bits) noexcept
{
    return (value << bits) | (value >> (32 - bits));
}

[[nodiscard]] std::array<std::byte, UcpLayout::digest_size> sha1(std::span<const std::byte> data)
{
    std::vector<std::uint8_t> message;
    message.reserve(data.size() + 72);
    for (std::byte b : data) {
        message.push_back(static_cast<std::uint8_t>(b));
    }

    const std::uint64_t bit_len = static_cast<std::uint64_t>(message.size()) * 8u;
    message.push_back(0x80u);
    while ((message.size() % 64u) != 56u) {
        message.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bit_len >> shift) & 0xFFu));
    }

    std::uint32_t h0 = 0x67452301u;
    std::uint32_t h1 = 0xEFCDAB89u;
    std::uint32_t h2 = 0x98BADCFEu;
    std::uint32_t h3 = 0x10325476u;
    std::uint32_t h4 = 0xC3D2E1F0u;

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::array<std::uint32_t, 80> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t off = chunk + i * 4;
            w[i] = (static_cast<std::uint32_t>(message[off]) << 24u) |
                   (static_cast<std::uint32_t>(message[off + 1]) << 16u) |
                   (static_cast<std::uint32_t>(message[off + 2]) << 8u) |
                   static_cast<std::uint32_t>(message[off + 3]);
        }
        for (std::size_t i = 16; i < 80; ++i) {
            w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (std::size_t i = 0; i < 80; ++i) {
            std::uint32_t f = 0;
            std::uint32_t k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }

            const std::uint32_t temp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<std::byte, UcpLayout::digest_size> digest{};
    const std::array<std::uint32_t, 5> words{h0, h1, h2, h3, h4};
    for (std::size_t i = 0; i < words.size(); ++i) {
        digest[i * 4] = static_cast<std::byte>((words[i] >> 24u) & 0xFFu);
        digest[i * 4 + 1] = static_cast<std::byte>((words[i] >> 16u) & 0xFFu);
        digest[i * 4 + 2] = static_cast<std::byte>((words[i] >> 8u) & 0xFFu);
        digest[i * 4 + 3] = static_cast<std::byte>(words[i] & 0xFFu);
    }
    return digest;
}

void write_digest(std::vector<std::byte>& buffer)
{
    if (buffer.size() < UcpLayout::header_size) {
        throw std::invalid_argument("Buffer is smaller than a UCP header.");
    }

    std::fill_n(buffer.begin() + UcpLayout::digest_offset, UcpLayout::digest_size, std::byte{0});
    const auto digest = sha1(buffer);
    std::copy(digest.begin(), digest.end(), buffer.begin() + UcpLayout::digest_offset);
}

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open UCP source file: " + path.string());
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("Could not size UCP source file: " + path.string());
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) {
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file) {
            throw std::runtime_error("Could not read UCP source file: " + path.string());
        }
    }
    return bytes;
}

} // namespace

bool is_ucp(std::span<const std::byte> data) noexcept
{
    return data.size() >= UcpLayout::header_size &&
           read_be32(data.data()) == UcpLayout::magic;
}

bool validate_ucp(std::span<const std::byte> data, std::string* error)
{
    auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };

    if (data.size() < UcpLayout::header_size) {
        return fail("Buffer is smaller than a UCP header.");
    }
    if (read_be32(data.data()) != UcpLayout::magic) {
        return fail("Bad UCP magic.");
    }
    if (read_be32(data.data() + 4) != UcpLayout::version) {
        return fail("Unsupported UCP version.");
    }

    const std::uint64_t total = read_be64(data.data() + 8);
    if (total != data.size()) {
        return fail("UCP size field does not match buffer length.");
    }

    const std::uint32_t record_size = read_be32(data.data() + UcpLayout::record_size_offset);
    if (record_size != UcpLayout::entry_record_size) {
        return fail("Unexpected UCP entry-record size.");
    }

    const std::uint64_t count = read_be32(data.data() + UcpLayout::count_offset);
    if (count > (std::numeric_limits<std::uint64_t>::max() - UcpLayout::header_size) /
                    UcpLayout::entry_record_size) {
        return fail("UCP entry table size overflows.");
    }

    const std::uint64_t table_end =
        UcpLayout::header_size + count * UcpLayout::entry_record_size;
    if (table_end > data.size()) {
        return fail("UCP entry table overruns the buffer.");
    }

    for (std::uint64_t i = 0; i < count; ++i) {
        const std::size_t rec =
            UcpLayout::header_size + static_cast<std::size_t>(i) * UcpLayout::entry_record_size;
        const std::uint64_t off = read_be64(data.data() + rec + UcpLayout::name_field_size);
        const std::uint64_t size = read_be64(data.data() + rec + UcpLayout::name_field_size + 8);
        if (off > total || size > total - off || off < table_end) {
            return fail("UCP entry range is outside the blob region.");
        }
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

bool verify_ucp_digest(std::span<const std::byte> data)
{
    if (data.size() < UcpLayout::header_size) {
        return false;
    }

    std::array<std::byte, UcpLayout::digest_size> stored{};
    std::copy_n(data.begin() + UcpLayout::digest_offset, UcpLayout::digest_size, stored.begin());

    std::vector<std::byte> copy(data.begin(), data.end());
    std::fill_n(copy.begin() + UcpLayout::digest_offset, UcpLayout::digest_size, std::byte{0});
    const auto calc = sha1(copy);
    return calc == stored;
}

std::vector<UcpEntry> read_ucp(std::span<const std::byte> data)
{
    std::string error;
    if (!validate_ucp(data, &error)) {
        throw std::runtime_error(error);
    }

    const std::uint32_t count = read_be32(data.data() + UcpLayout::count_offset);
    std::vector<UcpEntry> entries;
    entries.reserve(count);

    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t rec = UcpLayout::header_size + i * UcpLayout::entry_record_size;
        std::size_t name_len = 0;
        while (name_len < UcpLayout::name_field_size &&
               data[rec + name_len] != std::byte{0}) {
            ++name_len;
        }

        const std::uint64_t off = read_be64(data.data() + rec + UcpLayout::name_field_size);
        const std::uint64_t size = read_be64(data.data() + rec + UcpLayout::name_field_size + 8);

        UcpEntry entry;
        entry.name.assign(reinterpret_cast<const char*>(data.data() + rec), name_len);
        entry.data.assign(data.begin() + static_cast<std::ptrdiff_t>(off),
                          data.begin() + static_cast<std::ptrdiff_t>(off + size));
        entries.push_back(std::move(entry));
    }

    return entries;
}

std::vector<std::byte> build_ucp(std::span<const UcpEntry> entries)
{
    std::vector<UcpEntry> ordered(entries.begin(), entries.end());
    std::sort(ordered.begin(), ordered.end(), [](const UcpEntry& lhs, const UcpEntry& rhs) {
        return less_by_unsigned_bytes(lhs.name, rhs.name);
    });

    std::set<std::string, decltype(&less_by_unsigned_bytes)> seen(&less_by_unsigned_bytes);
    for (const UcpEntry& entry : ordered) {
        if (entry.name.empty()) {
            throw std::invalid_argument("A UCP entry name must not be empty.");
        }
        if (entry.name.size() > UcpLayout::name_field_size) {
            throw std::invalid_argument("A UCP entry name exceeds 32 bytes.");
        }
        if (!seen.insert(entry.name).second) {
            throw std::invalid_argument("Duplicate UCP entry name.");
        }
    }

    std::vector<std::size_t> offsets(ordered.size());
    std::size_t cursor = UcpLayout::header_size + ordered.size() * UcpLayout::entry_record_size;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        offsets[i] = cursor;
        cursor = align_up_strict(cursor + ordered[i].data.size());
    }
    const std::size_t total = ordered.empty() ? UcpLayout::header_size : cursor;

    std::vector<std::byte> buffer(total);
    write_be32(buffer.data(), UcpLayout::magic);
    write_be32(buffer.data() + 4, UcpLayout::version);
    write_be64(buffer.data() + 8, total);
    write_be32(buffer.data() + UcpLayout::count_offset, static_cast<std::uint32_t>(ordered.size()));
    write_be32(
        buffer.data() + UcpLayout::record_size_offset,
        static_cast<std::uint32_t>(UcpLayout::entry_record_size));

    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const std::size_t rec = UcpLayout::header_size + i * UcpLayout::entry_record_size;
        std::copy(
            ordered[i].name.begin(),
            ordered[i].name.end(),
            reinterpret_cast<char*>(buffer.data() + rec));
        write_be64(buffer.data() + rec + UcpLayout::name_field_size, offsets[i]);
        write_be64(buffer.data() + rec + UcpLayout::name_field_size + 8, ordered[i].data.size());

        std::copy(ordered[i].data.begin(), ordered[i].data.end(), buffer.begin() + offsets[i]);
    }

    write_digest(buffer);
    return buffer;
}

std::vector<std::byte> build_ucp(const std::vector<UcpEntry>& entries)
{
    return build_ucp(std::span<const UcpEntry>(entries.data(), entries.size()));
}

std::vector<std::byte> build_ucp_from_directory(const std::filesystem::path& directory)
{
    if (!std::filesystem::is_directory(directory)) {
        throw std::runtime_error("UCP source directory not found: " + directory.string());
    }

    std::vector<UcpEntry> entries;
    for (const auto& item : std::filesystem::directory_iterator(directory)) {
        if (!item.is_regular_file()) {
            continue;
        }
        entries.push_back(UcpEntry{item.path().filename().string(), read_file(item.path())});
    }
    return build_ucp(entries);
}

std::vector<std::byte> repair_ucp_digest(std::span<const std::byte> data)
{
    if (!is_ucp(data)) {
        throw std::runtime_error("Buffer is not a UCP archive.");
    }

    std::vector<std::byte> copy(data.begin(), data.end());
    write_digest(copy);
    return copy;
}

} // namespace prosperopkg
