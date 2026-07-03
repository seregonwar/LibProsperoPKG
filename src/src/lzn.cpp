// LibProsperoPkg - A library for building and inspecting PS5 packages.
// Copyright (C) 2026 seregonwar.

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

constexpr std::array<std::byte, 4> lzn_magic{
    std::byte{'L'}, std::byte{'Z'}, std::byte{'N'}, std::byte{'1'}};
constexpr std::size_t lzn_header_size = 24;
constexpr std::uint16_t lzn_version = 1;
constexpr std::uint16_t lzn_flag_raw = 1;
constexpr std::size_t min_match = 4;
constexpr std::size_t max_match = 131;
constexpr std::size_t max_literal_run = 128;
constexpr std::size_t hash_bits = 16;
constexpr std::size_t hash_size = 1u << hash_bits;
constexpr std::size_t max_distance = 0xFFFFu;

[[nodiscard]] std::uint16_t read_le16(std::span<const std::byte> data, std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[offset]) |
        (static_cast<std::uint16_t>(data[offset + 1]) << 8u));
}

[[nodiscard]] std::uint64_t read_le64(std::span<const std::byte> data, std::size_t offset) noexcept
{
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(data[offset + index]) << (index * 8u);
    }
    return value;
}

void append_le16(std::vector<std::byte>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
}

void append_le64(std::vector<std::byte>& out, std::uint64_t value)
{
    for (std::size_t index = 0; index < 8; ++index) {
        out.push_back(static_cast<std::byte>((value >> (index * 8u)) & 0xFFu));
    }
}

[[nodiscard]] std::uint32_t load_u32(std::span<const std::byte> data, std::size_t offset) noexcept
{
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

[[nodiscard]] std::uint32_t hash4(std::uint32_t value) noexcept
{
    return (value * 2654435761u) >> (32u - hash_bits);
}

void append_header(
    std::vector<std::byte>& out,
    std::uint16_t flags,
    std::uint64_t original_size,
    std::uint64_t payload_size)
{
    out.insert(out.end(), lzn_magic.begin(), lzn_magic.end());
    append_le16(out, lzn_version);
    append_le16(out, flags);
    append_le64(out, original_size);
    append_le64(out, payload_size);
}

void emit_literals(std::vector<std::byte>& out, std::span<const std::byte> input)
{
    std::size_t cursor = 0;
    while (cursor < input.size()) {
        const std::size_t chunk = std::min(max_literal_run, input.size() - cursor);
        out.push_back(static_cast<std::byte>(chunk - 1u));
        out.insert(out.end(), input.begin() + static_cast<std::ptrdiff_t>(cursor),
                   input.begin() + static_cast<std::ptrdiff_t>(cursor + chunk));
        cursor += chunk;
    }
}

void emit_match(std::vector<std::byte>& out, std::size_t length, std::size_t distance)
{
    while (length > 0) {
        const std::size_t chunk = std::min(max_match, length);
        if (chunk < min_match || distance == 0 || distance > max_distance) {
            throw std::runtime_error("Internal LZN match is out of range.");
        }
        out.push_back(static_cast<std::byte>(0x80u | static_cast<unsigned char>(chunk - min_match)));
        append_le16(out, static_cast<std::uint16_t>(distance));
        length -= chunk;
    }
}

[[nodiscard]] std::vector<std::byte> compress_payload(std::span<const std::byte> input, int level)
{
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw std::runtime_error("LZN input is too large for this encoder.");
    }

    const std::size_t lazy_limit = level <= 1 ? 0u : static_cast<std::size_t>(std::min(level, 4));
    std::vector<int> table(hash_size, -1);
    std::vector<std::byte> out;
    out.reserve(input.size());

    std::size_t anchor = 0;
    std::size_t pos = 0;
    while (pos + min_match <= input.size()) {
        const std::uint32_t key = hash4(load_u32(input, pos));
        const int candidate = table[key];
        table[key] = static_cast<int>(pos);

        std::size_t best_len = 0;
        std::size_t best_distance = 0;
        if (candidate >= 0) {
            const auto previous = static_cast<std::size_t>(candidate);
            const std::size_t distance = pos - previous;
            if (distance <= max_distance &&
                std::equal(input.begin() + static_cast<std::ptrdiff_t>(previous),
                           input.begin() + static_cast<std::ptrdiff_t>(previous + min_match),
                           input.begin() + static_cast<std::ptrdiff_t>(pos))) {
                best_len = min_match;
                const std::size_t limit = std::min(max_match, input.size() - pos);
                while (best_len < limit && input[previous + best_len] == input[pos + best_len]) {
                    ++best_len;
                }
                best_distance = distance;
            }
        }

        if (best_len >= min_match && lazy_limit > 0 && pos + 1 + min_match <= input.size()) {
            for (std::size_t lookahead = 1; lookahead <= lazy_limit && pos + lookahead + min_match <= input.size();
                 ++lookahead) {
                const std::uint32_t lazy_key = hash4(load_u32(input, pos + lookahead));
                const int lazy_candidate = table[lazy_key];
                if (lazy_candidate < 0) {
                    continue;
                }
                const auto previous = static_cast<std::size_t>(lazy_candidate);
                const std::size_t distance = pos + lookahead - previous;
                if (distance == 0 || distance > max_distance) {
                    continue;
                }
                if (!std::equal(input.begin() + static_cast<std::ptrdiff_t>(previous),
                                input.begin() + static_cast<std::ptrdiff_t>(previous + min_match),
                                input.begin() + static_cast<std::ptrdiff_t>(pos + lookahead))) {
                    continue;
                }
                std::size_t lazy_len = min_match;
                const std::size_t limit = std::min(max_match, input.size() - pos - lookahead);
                while (lazy_len < limit && input[previous + lazy_len] == input[pos + lookahead + lazy_len]) {
                    ++lazy_len;
                }
                if (lazy_len > best_len + lookahead) {
                    best_len = 0;
                    break;
                }
            }
        }

        if (best_len >= min_match) {
            emit_literals(out, input.subspan(anchor, pos - anchor));
            emit_match(out, best_len, best_distance);

            const std::size_t end = pos + best_len;
            for (++pos; pos < end && pos + min_match <= input.size(); ++pos) {
                table[hash4(load_u32(input, pos))] = static_cast<int>(pos);
            }
            anchor = end;
            pos = end;
        } else {
            ++pos;
        }
    }

    emit_literals(out, input.subspan(anchor));
    return out;
}

[[nodiscard]] std::size_t decompress_payload_to(
    std::span<const std::byte> frame,
    const LznFrameInfo& info,
    std::span<std::byte> output)
{
    if (info.original_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("LZN frame is too large for this host.");
    }

    const auto original_size = static_cast<std::size_t>(info.original_size);
    if (output.size() < original_size) {
        throw std::runtime_error("LZN output buffer is too small.");
    }

    const auto payload = frame.subspan(lzn_header_size, static_cast<std::size_t>(info.payload_size));
    if (info.stored_raw()) {
        if (payload.size() != original_size) {
            throw std::runtime_error("Raw LZN frame size does not match the header.");
        }
        if (!payload.empty()) {
            std::memcpy(output.data(), payload.data(), payload.size());
        }
        return payload.size();
    }

    std::size_t cursor = 0;
    std::size_t written = 0;
    while (cursor < payload.size()) {
        const auto token = static_cast<unsigned char>(payload[cursor++]);
        if ((token & 0x80u) == 0) {
            const std::size_t length = (token & 0x7Fu) + 1u;
            if (length > payload.size() - cursor) {
                throw std::runtime_error("LZN literal run is truncated.");
            }
            if (written + length > original_size) {
                throw std::runtime_error("LZN literal run exceeds the declared output size.");
            }
            std::memcpy(output.data() + written, payload.data() + cursor, length);
            cursor += length;
            written += length;
        } else {
            const std::size_t length = (token & 0x7Fu) + min_match;
            if (payload.size() - cursor < 2) {
                throw std::runtime_error("LZN match token is truncated.");
            }
            const std::size_t distance = read_le16(payload, cursor);
            cursor += 2;
            if (distance == 0 || distance > written) {
                throw std::runtime_error("LZN match distance is invalid.");
            }
            if (written + length > original_size) {
                throw std::runtime_error("LZN match exceeds the declared output size.");
            }
            if (distance >= length) {
                std::memcpy(output.data() + written, output.data() + written - distance, length);
                written += length;
            } else {
                for (std::size_t index = 0; index < length; ++index) {
                    output[written] = output[written - distance];
                    ++written;
                }
            }
        }
    }

    if (written != original_size) {
        throw std::runtime_error("LZN frame ended before the declared output size.");
    }
    return written;
}

} // namespace

bool is_lzn_frame(std::span<const std::byte> data) noexcept
{
    return data.size() >= lzn_header_size &&
           std::equal(lzn_magic.begin(), lzn_magic.end(), data.begin());
}

LznFrameInfo read_lzn_frame_info(std::span<const std::byte> data)
{
    if (!is_lzn_frame(data)) {
        throw std::runtime_error("Input is not an LZN1 frame.");
    }

    LznFrameInfo info;
    info.version = read_le16(data, 0x04);
    info.flags = read_le16(data, 0x06);
    info.original_size = read_le64(data, 0x08);
    info.payload_size = read_le64(data, 0x10);
    if (info.version != lzn_version) {
        throw std::runtime_error("Unsupported LZN frame version.");
    }
    if ((info.flags & ~lzn_flag_raw) != 0) {
        throw std::runtime_error("Unsupported LZN frame flags.");
    }
    if (info.payload_size != data.size() - lzn_header_size) {
        throw std::runtime_error("LZN frame payload size does not match the container size.");
    }
    return info;
}

std::vector<std::byte> lzn_compress(std::span<const std::byte> input, int level)
{
    const int clamped_level = std::clamp(level, 1, 4);
    auto payload = compress_payload(input, clamped_level);

    const bool store_raw = payload.size() >= input.size();
    std::vector<std::byte> out;
    out.reserve(lzn_header_size + (store_raw ? input.size() : payload.size()));
    append_header(
        out,
        store_raw ? lzn_flag_raw : 0,
        input.size(),
        store_raw ? input.size() : payload.size());
    if (store_raw) {
        out.insert(out.end(), input.begin(), input.end());
    } else {
        out.insert(out.end(), payload.begin(), payload.end());
    }
    return out;
}

std::size_t lzn_decompress_to(std::span<const std::byte> frame, std::span<std::byte> output)
{
    const auto info = read_lzn_frame_info(frame);
    return decompress_payload_to(frame, info, output);
}

std::vector<std::byte> lzn_decompress(std::span<const std::byte> frame)
{
    const auto info = read_lzn_frame_info(frame);
    if (info.original_size > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("LZN frame is too large for this host.");
    }
    std::vector<std::byte> out(static_cast<std::size_t>(info.original_size));
    (void)decompress_payload_to(frame, info, out);
    return out;
}

} // namespace prosperopkg
