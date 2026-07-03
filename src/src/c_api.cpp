// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/libprosperopkg.h>

#include <prosperopkg/prosperopkg.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef PROSPEROPKG_VERSION
#define PROSPEROPKG_VERSION "0.1.0"
#endif

namespace {

thread_local std::string g_last_error;
thread_local int g_cached_fself_length = 0;
thread_local std::vector<std::byte> g_cached_fself_source;
thread_local std::vector<std::byte> g_cached_fself;

constexpr std::uint64_t pfs_header_version_ps5 = 2;
constexpr std::uint64_t pfs_header_magic = 20130315;
constexpr std::size_t pfs_header_min_size = 0x380;
constexpr std::size_t pfs_mode_offset = 0x1C;
constexpr std::size_t pfs_block_size_offset = 0x20;
constexpr std::size_t pfs_seed_offset = 0x370;
constexpr std::uint16_t pfs_mode_encrypted = 0x4;

void set_error(std::string message)
{
    g_last_error = std::move(message);
}

void clear_error()
{
    g_last_error.clear();
}

[[nodiscard]] int copy_string(std::string_view value, char* buffer, int capacity)
{
    if (capacity < 0) {
        set_error("capacity is negative");
        return -1;
    }

    const auto required = static_cast<int>(value.size() + 1u);
    if (buffer == nullptr || capacity == 0) {
        return -required;
    }
    if (capacity < required) {
        buffer[0] = '\0';
        return -required;
    }

    std::memcpy(buffer, value.data(), value.size());
    buffer[value.size()] = '\0';
    return static_cast<int>(value.size());
}

[[nodiscard]] char upper_ascii(char ch) noexcept
{
    return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - 'a' + 'A') : ch;
}

[[nodiscard]] bool is_ascii_upper(char ch) noexcept
{
    return ch >= 'A' && ch <= 'Z';
}

[[nodiscard]] bool is_ascii_digit(char ch) noexcept
{
    return ch >= '0' && ch <= '9';
}

[[nodiscard]] bool is_ascii_alnum_upper(char ch) noexcept
{
    return is_ascii_upper(ch) || is_ascii_digit(ch);
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
    return static_cast<std::uint64_t>(read_le32(data, offset)) |
           (static_cast<std::uint64_t>(read_le32(data, offset + 4)) << 32u);
}

[[nodiscard]] bool all_zero(std::span<const std::byte> data) noexcept
{
    return std::all_of(data.begin(), data.end(), [](std::byte value) {
        return value == std::byte{0};
    });
}

[[nodiscard]] bool exact_c_string_length(const char* value, std::size_t length) noexcept
{
    for (std::size_t index = 0; index < length; ++index) {
        if (value[index] == '\0') {
            return false;
        }
    }
    return value[length] == '\0';
}

[[nodiscard]] bool valid_content_id_cstr(const char* value) noexcept
{
    return exact_c_string_length(value, 36) &&
           is_ascii_upper(value[0]) &&
           is_ascii_upper(value[1]) &&
           is_ascii_digit(value[2]) &&
           is_ascii_digit(value[3]) &&
           is_ascii_digit(value[4]) &&
           is_ascii_digit(value[5]) &&
           value[6] == '-' &&
           is_ascii_upper(value[7]) &&
           is_ascii_upper(value[8]) &&
           is_ascii_upper(value[9]) &&
           is_ascii_upper(value[10]) &&
           is_ascii_digit(value[11]) &&
           is_ascii_digit(value[12]) &&
           is_ascii_digit(value[13]) &&
           is_ascii_digit(value[14]) &&
           is_ascii_digit(value[15]) &&
           value[16] == '_' &&
           value[17] == '0' &&
           value[18] == '0' &&
           value[19] == '-' &&
           is_ascii_alnum_upper(value[20]) &&
           is_ascii_alnum_upper(value[21]) &&
           is_ascii_alnum_upper(value[22]) &&
           is_ascii_alnum_upper(value[23]) &&
           is_ascii_alnum_upper(value[24]) &&
           is_ascii_alnum_upper(value[25]) &&
           is_ascii_alnum_upper(value[26]) &&
           is_ascii_alnum_upper(value[27]) &&
           is_ascii_alnum_upper(value[28]) &&
           is_ascii_alnum_upper(value[29]) &&
           is_ascii_alnum_upper(value[30]) &&
           is_ascii_alnum_upper(value[31]) &&
           is_ascii_alnum_upper(value[32]) &&
           is_ascii_alnum_upper(value[33]) &&
           is_ascii_alnum_upper(value[34]) &&
           is_ascii_alnum_upper(value[35]);
}

[[nodiscard]] bool valid_title_id_cstr(const char* value) noexcept
{
    return exact_c_string_length(value, 9) &&
           is_ascii_upper(value[0]) &&
           is_ascii_upper(value[1]) &&
           is_ascii_upper(value[2]) &&
           is_ascii_upper(value[3]) &&
           is_ascii_digit(value[4]) &&
           is_ascii_digit(value[5]) &&
           is_ascii_digit(value[6]) &&
           is_ascii_digit(value[7]) &&
           is_ascii_digit(value[8]);
}

[[nodiscard]] std::uint64_t stream_size(std::fstream& file)
{
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        throw std::runtime_error("could not determine file size");
    }
    file.seekg(0, std::ios::beg);
    return static_cast<std::uint64_t>(end);
}

void copy_upper_trim_pad(char* out, const char* input, const char* fallback, std::size_t size, char pad) noexcept
{
    const char* src = (input == nullptr || input[0] == '\0') ? fallback : input;
    std::size_t written = 0;
    while (written < size && src[written] != '\0') {
        out[written] = upper_ascii(src[written]);
        ++written;
    }
    while (written < size) {
        out[written++] = pad;
    }
}

[[nodiscard]] std::span<const std::byte> as_bytes(const unsigned char* data, int length)
{
    if (data == nullptr || length <= 0) {
        return {};
    }
    return {reinterpret_cast<const std::byte*>(data), static_cast<std::size_t>(length)};
}

[[nodiscard]] int package_type_to_c(prosperopkg::PackageType type) noexcept
{
    switch (type) {
    case prosperopkg::PackageType::meta:
        return LPP_TYPE_META;
    case prosperopkg::PackageType::full_retail:
        return LPP_TYPE_FULL_RETAIL;
    case prosperopkg::PackageType::full_debug:
        return LPP_TYPE_FULL_DEBUG;
    }
    return -1;
}

[[nodiscard]] prosperopkg::BuildMode build_mode_from_c(int mode)
{
    switch (mode) {
    case LPP_MODE_APPLICATION:
        return prosperopkg::BuildMode::application;
    case LPP_MODE_HOMEBREW:
        return prosperopkg::BuildMode::homebrew;
    case LPP_MODE_ADDITIONAL_CONTENT_DATA:
        return prosperopkg::BuildMode::additional_content_data;
    case LPP_MODE_ADDITIONAL_CONTENT_NO_DATA:
        return prosperopkg::BuildMode::additional_content_no_data;
    default:
        throw std::invalid_argument("unknown package build mode");
    }
}

[[nodiscard]] prosperopkg::BuildOutputFormat output_format_from_c(int output_format)
{
    switch (output_format) {
    case LPP_OUTPUT_METADATA_CONTAINER:
        return prosperopkg::BuildOutputFormat::metadata_container;
    case LPP_OUTPUT_DEBUG_IMAGE:
        return prosperopkg::BuildOutputFormat::debug_image;
    default:
        throw std::invalid_argument("unknown package output format");
    }
}

[[nodiscard]] prosperopkg::InnerCompression inner_compression_from_c(int inner_compression)
{
    switch (inner_compression) {
    case LPP_INNER_NONE:
        return prosperopkg::InnerCompression::none;
    case LPP_INNER_ZLIB:
        return prosperopkg::InnerCompression::zlib;
    case LPP_INNER_KRAKEN:
        return prosperopkg::InnerCompression::kraken;
    default:
        throw std::invalid_argument("unknown inner compression mode");
    }
}

[[nodiscard]] prosperopkg::InnerImageForm inner_form_from_c(int form)
{
    switch (form) {
    case LPP_FORM_PLAINTEXT:
        return prosperopkg::InnerImageForm::plaintext;
    case LPP_FORM_ENCRYPTED:
        return prosperopkg::InnerImageForm::encrypted;
    case LPP_FORM_COMPRESSED:
        return prosperopkg::InnerImageForm::compressed;
    case LPP_FORM_KRAKEN_COMPRESSED:
        return prosperopkg::InnerImageForm::kraken_compressed;
    default:
        throw std::invalid_argument("unknown inner image form");
    }
}

template <typename Fn>
[[nodiscard]] auto guard(Fn&& fn) noexcept -> decltype(fn())
{
    try {
        return fn();
    } catch (const std::exception& ex) {
        set_error(ex.what());
        return decltype(fn()){-1};
    } catch (...) {
        set_error("unknown C++ exception");
        return decltype(fn()){-1};
    }
}

} // namespace

extern "C" {

const char* lpp_version(void)
{
    return "LibProsperoPkg C++ " PROSPEROPKG_VERSION;
}

int lpp_last_error(char* buffer, int capacity)
{
    return copy_string(g_last_error, buffer, capacity);
}

int lpp_is_valid_content_id(const char* content_id)
{
    if (content_id == nullptr) {
        return 0;
    }
    return valid_content_id_cstr(content_id) ? 1 : 0;
}

int lpp_is_valid_title_id(const char* title_id)
{
    if (title_id == nullptr) {
        return 0;
    }
    return valid_title_id_cstr(title_id) ? 1 : 0;
}

int lpp_compose_content_id(const char* publisher,
                           const char* title_id,
                           const char* label,
                           char* out_buffer,
                           int capacity)
{
    constexpr int required = 37;
    if (capacity < 0) {
        set_error("capacity is negative");
        return -1;
    }
    if (out_buffer == nullptr || capacity == 0) {
        return -required;
    }
    if (capacity < required) {
        out_buffer[0] = '\0';
        return -required;
    }

    std::array<char, required> composed{};
    copy_upper_trim_pad(composed.data(), publisher, "UP9000", 6, '0');
    composed[6] = '-';
    copy_upper_trim_pad(composed.data() + 7, title_id, "PPSA00000", 9, '0');
    composed[16] = '_';
    composed[17] = '0';
    composed[18] = '0';
    composed[19] = '-';

    std::size_t label_out = 20;
    if (label != nullptr) {
        for (const char* it = label; *it != '\0' && label_out < 36; ++it) {
            const char ch = upper_ascii(*it);
            if (is_ascii_alnum_upper(ch)) {
                composed[label_out++] = ch;
            }
        }
    }
    while (label_out < 36) {
        composed[label_out++] = '0';
    }
    composed[36] = '\0';

    std::memcpy(out_buffer, composed.data(), composed.size());
    clear_error();
    return 36;
}

int lpp_build_package(const char* source_folder,
                      const char* output_folder,
                      const char* content_id,
                      const char* passcode,
                      const char* title,
                      const char* title_id,
                      const char* version,
                      int mode,
                      int output_format,
                      int inner_compression,
                      char* out_path,
                      int out_path_capacity)
{
    if (source_folder == nullptr || source_folder[0] == '\0') {
        set_error("source folder is empty");
        return -1;
    }
    if (output_folder == nullptr || output_folder[0] == '\0') {
        set_error("output folder is empty");
        return -1;
    }
    if (content_id == nullptr || content_id[0] == '\0') {
        set_error("content id is empty");
        return -1;
    }
    if (passcode == nullptr || passcode[0] == '\0') {
        set_error("passcode is empty");
        return -1;
    }

    return guard([&]() {
        const auto path = prosperopkg::build_package(prosperopkg::PackageBuildOptions{
            std::filesystem::path{source_folder},
            std::filesystem::path{output_folder},
            content_id,
            passcode,
            title == nullptr ? std::string{} : std::string{title},
            title_id == nullptr ? std::string{} : std::string{title_id},
            version == nullptr || version[0] == '\0' ? std::string{"01.00"} : std::string{version},
            build_mode_from_c(mode),
            output_format_from_c(output_format),
            inner_compression_from_c(inner_compression)});
        const int copied = copy_string(path.string(), out_path, out_path_capacity);
        if (copied < 0) {
            return copied;
        }
        clear_error();
        return 0;
    });
}

int lpp_detect_package_type(const char* path)
{
    if (path == nullptr || path[0] == '\0') {
        set_error("path is empty");
        return -1;
    }

    return guard([&]() {
        const auto type = prosperopkg::detect_type(std::filesystem::path{path});
        if (!type.has_value()) {
            set_error("not a recognized PS5 package");
            return -1;
        }
        clear_error();
        return package_type_to_c(*type);
    });
}

int lpp_build_inner_image(const char* source_folder,
                          const char* output_path,
                          const char* content_id,
                          const char* passcode,
                          int form,
                          char* out_path,
                          int out_path_capacity)
{
    if (source_folder == nullptr || source_folder[0] == '\0') {
        set_error("source folder is empty");
        return -1;
    }
    if (output_path == nullptr || output_path[0] == '\0') {
        set_error("output path is empty");
        return -1;
    }
    if (content_id == nullptr || content_id[0] == '\0') {
        set_error("content id is empty");
        return -1;
    }
    if (passcode == nullptr || passcode[0] == '\0') {
        set_error("passcode is empty");
        return -1;
    }

    return guard([&]() {
        const auto path = prosperopkg::build_inner_image(prosperopkg::InnerImageBuildOptions{
            std::filesystem::path{source_folder},
            std::filesystem::path{output_path},
            content_id,
            passcode,
            inner_form_from_c(form)});
        const int copied = copy_string(path.string(), out_path, out_path_capacity);
        if (copied < 0) {
            return copied;
        }
        clear_error();
        return 0;
    });
}

int lpp_encrypt_pfs_image(const char* pfs_image_path, const char* content_id, const char* passcode)
{
    if (pfs_image_path == nullptr || pfs_image_path[0] == '\0') {
        set_error("PFS image path is empty");
        return -1;
    }
    if (content_id == nullptr || content_id[0] == '\0') {
        set_error("content id is empty");
        return -1;
    }
    if (passcode == nullptr || passcode[0] == '\0') {
        set_error("passcode is empty");
        return -1;
    }

    return guard([&]() {
        std::fstream file(std::filesystem::path{pfs_image_path}, std::ios::binary | std::ios::in | std::ios::out);
        if (!file) {
            throw std::runtime_error("could not open PFS image: " + std::string(pfs_image_path));
        }

        const std::uint64_t image_size = stream_size(file);
        if (image_size < pfs_header_min_size) {
            throw std::runtime_error("PFS image is too small for a PS5 superblock");
        }
        if ((image_size % prosperopkg::pfs_inner_xts_sector_size) != 0) {
            throw std::runtime_error("PFS image size must be a multiple of 4096 bytes");
        }

        std::array<std::byte, pfs_header_min_size> header{};
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        if (file.gcount() != static_cast<std::streamsize>(header.size())) {
            throw std::runtime_error("could not read PFS superblock");
        }

        if (read_le64(header, 0) != pfs_header_version_ps5 || read_le64(header, 8) != pfs_header_magic) {
            throw std::runtime_error("invalid PS5 PFS superblock");
        }

        const auto block_size = static_cast<std::size_t>(read_le32(header, pfs_block_size_offset));
        if (block_size == 0 ||
            (block_size % prosperopkg::pfs_inner_xts_sector_size) != 0 ||
            block_size > image_size) {
            throw std::runtime_error("unsupported PFS block size");
        }

        std::array<std::byte, 16> seed{};
        std::copy_n(header.begin() + static_cast<std::ptrdiff_t>(pfs_seed_offset), seed.size(), seed.begin());
        if (all_zero(seed)) {
            throw std::runtime_error("PFS image seed is empty; write a deterministic 16-byte seed before encryption");
        }

        const auto ekpfs = prosperopkg::compute_package_key(
            content_id,
            passcode,
            1,
            prosperopkg::PackageKeyDigest::sha256);
        const auto keys = prosperopkg::derive_pfs_encryption_keys(ekpfs, seed, false);

        const auto mode = static_cast<std::uint16_t>(read_le16(header, pfs_mode_offset) | pfs_mode_encrypted);
        const std::array<char, 2> mode_bytes{
            static_cast<char>(mode & 0xFFu),
            static_cast<char>((mode >> 8u) & 0xFFu),
        };
        file.seekp(static_cast<std::streamoff>(pfs_mode_offset), std::ios::beg);
        file.write(mode_bytes.data(), static_cast<std::streamsize>(mode_bytes.size()));
        if (!file) {
            throw std::runtime_error("could not patch PFS encrypted mode bit");
        }

        std::array<std::byte, prosperopkg::pfs_inner_xts_sector_size> sector{};
        const std::uint64_t start_sector = block_size / prosperopkg::pfs_inner_xts_sector_size;
        const std::uint64_t total_sectors = image_size / prosperopkg::pfs_inner_xts_sector_size;
        for (std::uint64_t sector_index = start_sector; sector_index < total_sectors; ++sector_index) {
            const auto offset = static_cast<std::streamoff>(sector_index * prosperopkg::pfs_inner_xts_sector_size);
            file.seekg(offset, std::ios::beg);
            file.read(reinterpret_cast<char*>(sector.data()), static_cast<std::streamsize>(sector.size()));
            if (file.gcount() != static_cast<std::streamsize>(sector.size())) {
                throw std::runtime_error("could not read PFS sector");
            }

            prosperopkg::xts_transform_unit(sector, keys.data_key, keys.tweak_key, sector_index, true);

            file.seekp(offset, std::ios::beg);
            file.write(reinterpret_cast<const char*>(sector.data()), static_cast<std::streamsize>(sector.size()));
            if (!file) {
                throw std::runtime_error("could not write encrypted PFS sector");
            }
        }

        file.flush();
        if (!file) {
            throw std::runtime_error("could not flush encrypted PFS image");
        }

        clear_error();
        return 0;
    });
}

int lpp_pack_pfs_image(const char* input_image_path, const char* output_path, int level, int block_size)
{
    if (input_image_path == nullptr || input_image_path[0] == '\0') {
        set_error("input path is empty");
        return -1;
    }
    if (output_path == nullptr || output_path[0] == '\0') {
        set_error("output path is empty");
        return -1;
    }

    return guard([&]() {
        if (level < 0) {
            prosperopkg::pack_pfsc_pfs_v3_stored(
                std::filesystem::path{input_image_path},
                std::filesystem::path{output_path},
                -level,
                block_size <= 0 ? 0x40000u : static_cast<std::uint32_t>(block_size));
        } else {
            prosperopkg::pack_pfsc_zlib(
                std::filesystem::path{input_image_path},
                std::filesystem::path{output_path},
                level,
                block_size <= 0 ? 0x10000u : static_cast<std::uint32_t>(block_size));
        }
        clear_error();
        return 0;
    });
}

long long lpp_unpack_pfs_image(const char* input_path, const char* output_path)
{
    if (input_path == nullptr || input_path[0] == '\0') {
        set_error("input path is empty");
        return -1;
    }
    if (output_path == nullptr || output_path[0] == '\0') {
        set_error("output path is empty");
        return -1;
    }

    return guard([&]() {
        const auto written = prosperopkg::unpack_pfsc(
            std::filesystem::path{input_path},
            std::filesystem::path{output_path});
        clear_error();
        return static_cast<long long>(written);
    });
}

int lpp_is_self(const unsigned char* data, int length)
{
    return prosperopkg::is_self(as_bytes(data, length)) ? 1 : 0;
}

int lpp_is_elf(const unsigned char* data, int length)
{
    return prosperopkg::is_elf(as_bytes(data, length)) ? 1 : 0;
}

int lpp_is_ucp(const unsigned char* data, int length)
{
    return prosperopkg::is_ucp(as_bytes(data, length)) ? 1 : 0;
}

int lpp_make_fself(const unsigned char* elf, int elf_length, unsigned char* out_buffer, int capacity)
{
    if (elf == nullptr || elf_length <= 0) {
        set_error("ELF buffer is empty");
        return -1;
    }
    if (capacity < 0) {
        set_error("capacity is negative");
        return -1;
    }

    return guard([&]() {
        const auto input = as_bytes(elf, elf_length);
        const bool cache_hit =
            g_cached_fself_length == elf_length &&
            !g_cached_fself.empty() &&
            g_cached_fself_source.size() == static_cast<std::size_t>(elf_length) &&
            std::memcmp(g_cached_fself_source.data(), elf, static_cast<std::size_t>(elf_length)) == 0;
        if (!cache_hit) {
            g_cached_fself = prosperopkg::make_fself(input);
            g_cached_fself_source.assign(input.begin(), input.end());
            g_cached_fself_length = elf_length;
        }

        const auto& self = g_cached_fself;
        const auto required = static_cast<int>(self.size());
        if (out_buffer == nullptr || capacity == 0) {
            clear_error();
            return required;
        }
        if (capacity < required) {
            set_error("buffer too small (need " + std::to_string(required) + ")");
            return -1;
        }

        std::memcpy(out_buffer, self.data(), self.size());
        clear_error();
        return required;
    });
}

} // extern "C"
