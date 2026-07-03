// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/build.hpp>

#include <prosperopkg/aes_xts.hpp>
#include <prosperopkg/content_id.hpp>
#include <prosperopkg/crc32c.hpp>
#include <prosperopkg/hash.hpp>
#include <prosperopkg/pfsc.hpp>
#include <prosperopkg/pfs_image.hpp>
#include <prosperopkg/pfs_keys.hpp>
#include <prosperopkg/pkg.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace prosperopkg {
namespace {

constexpr std::size_t pfs_block_size = 0x10000;
constexpr std::size_t pfs_header_size = 0x10000;
constexpr std::uint64_t pfs_header_version_ps5 = 2;
constexpr std::uint64_t pfs_header_magic = 20130315;
constexpr std::size_t pfs_mode_offset = 0x1C;
constexpr std::size_t pfs_block_size_offset = 0x20;
constexpr std::size_t pfs_seed_offset = 0x370;
constexpr std::size_t pfs_unknown_index_offset = 0x36C;
constexpr std::uint16_t pfs_mode_unknown_always_set = 0x8;
constexpr std::uint16_t pfs_mode_encrypted = 0x4;
constexpr std::uint32_t cnt_flags_ps5 = 0x02000001u;
constexpr std::uint32_t cnt_drm_type_ps5 = 0x10u;
constexpr std::uint32_t content_type_game = 0x20u;
constexpr std::uint32_t content_type_additional_data = 0x21u;
constexpr std::uint32_t content_type_additional_no_data = 0x22u;
constexpr std::uint32_t entry_id_native_inner_image = 0x3000u;
constexpr std::uint32_t entry_id_native_build_manifest = 0x3001u;
constexpr std::array<char, 8> inner_index_magic{'L', 'P', 'F', 'S', 'I', 'D', 'X', '1'};

struct SourceFile {
    std::filesystem::path full_path;
    std::string relative_path;
    std::uint64_t size = 0;
    std::uint64_t image_offset = 0;
    std::uint32_t crc = 0;
};

[[nodiscard]] std::uint64_t align_up(std::uint64_t value, std::uint64_t alignment) noexcept
{
    return ((value + alignment - 1u) / alignment) * alignment;
}

void ensure_parent(const std::filesystem::path& path)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
}

[[nodiscard]] bool excluded_source_name(std::string_view name)
{
    return name == "keystone" ||
           name == "disc_info.dat" ||
           name == "pfs-version.dat" ||
           name == "ext_info.dat";
}

[[nodiscard]] bool excluded_source_suffix(std::string_view name)
{
    const auto has_suffix = [&](std::string_view suffix) {
        return name.size() >= suffix.size() &&
               std::equal(suffix.rbegin(), suffix.rend(), name.rbegin(), [](char lhs, char rhs) {
                   const auto lower = [](char ch) {
                       return ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch - 'A' + 'a') : ch;
                   };
                   return lower(lhs) == lower(rhs);
               });
    };
    return has_suffix(".gp4") || has_suffix(".gp5") || has_suffix(".esbak") || has_suffix(".dds");
}

[[nodiscard]] std::string relative_generic_string(
    const std::filesystem::path& full_path,
    const std::filesystem::path& root)
{
    return std::filesystem::relative(full_path, root).generic_string();
}

[[nodiscard]] std::uint32_t file_crc32c(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open source file: " + path.string());
    }

    std::array<std::byte, 64 * 1024> buffer{};
    std::uint32_t crc = 0xFFFFFFFFu;
    while (file) {
        file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const auto got = file.gcount();
        if (got > 0) {
            crc = crc32c_update(crc, std::span<const std::byte>(buffer.data(), static_cast<std::size_t>(got)));
        }
    }
    return ~crc;
}

[[nodiscard]] std::vector<SourceFile> enumerate_source_files(const std::filesystem::path& source_folder)
{
    if (!std::filesystem::is_directory(source_folder)) {
        throw std::invalid_argument("Source folder does not exist: " + source_folder.string());
    }

    std::vector<SourceFile> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(source_folder)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        const std::string name = entry.path().filename().string();
        if (excluded_source_name(name) || excluded_source_suffix(name)) {
            continue;
        }

        SourceFile file;
        file.full_path = entry.path();
        file.relative_path = relative_generic_string(entry.path(), source_folder);
        file.size = entry.file_size();
        file.crc = file_crc32c(entry.path());
        files.push_back(std::move(file));
    }

    std::sort(files.begin(), files.end(), [](const SourceFile& lhs, const SourceFile& rhs) {
        return lhs.relative_path < rhs.relative_path;
    });
    return files;
}

void append_le16(std::vector<std::byte>& out, std::uint16_t value)
{
    out.push_back(static_cast<std::byte>(value & 0xFFu));
    out.push_back(static_cast<std::byte>((value >> 8u) & 0xFFu));
}

void append_le32(std::vector<std::byte>& out, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
    }
}

void append_le64(std::vector<std::byte>& out, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        out.push_back(static_cast<std::byte>((value >> (i * 8)) & 0xFFu));
    }
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

[[nodiscard]] std::array<std::byte, 16> deterministic_seed(
    std::string_view content_id,
    std::string_view passcode,
    std::span<const SourceFile> files)
{
    std::vector<std::byte> material;
    const auto add_text = [&](std::string_view text) {
        for (char ch : text) {
            material.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
        material.push_back(std::byte{0});
    };
    add_text("LibProsperoPkg.NativeInnerSeed.v1");
    add_text(content_id);
    add_text(passcode);
    for (const auto& file : files) {
        add_text(file.relative_path);
        append_le64(material, file.size);
        append_le32(material, file.crc);
    }

    const auto digest = sha256(material);
    std::array<std::byte, 16> seed{};
    std::copy_n(digest.begin(), seed.size(), seed.begin());
    return seed;
}

[[nodiscard]] std::vector<std::byte> build_index(std::span<const SourceFile> files, std::uint64_t data_start)
{
    std::vector<std::byte> index;
    index.reserve(64 + files.size() * 64);
    for (char ch : inner_index_magic) {
        index.push_back(static_cast<std::byte>(ch));
    }
    append_le32(index, 1);
    append_le32(index, static_cast<std::uint32_t>(files.size()));
    append_le64(index, pfs_block_size);
    append_le64(index, data_start);

    for (const SourceFile& file : files) {
        if (file.relative_path.size() > 0xFFFFu) {
            throw std::invalid_argument("Source relative path is too long: " + file.relative_path);
        }
        append_le16(index, static_cast<std::uint16_t>(file.relative_path.size()));
        append_le16(index, 0);
        append_le32(index, file.crc);
        append_le64(index, file.image_offset);
        append_le64(index, file.size);
        for (char ch : file.relative_path) {
            index.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
        }
    }
    return index;
}

void copy_file_to_stream(const std::filesystem::path& path, std::ostream& out)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open source file: " + path.string());
    }

    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto got = input.gcount();
        if (got > 0) {
            out.write(buffer.data(), got);
        }
    }
    if (!out) {
        throw std::runtime_error("Could not write source file payload: " + path.string());
    }
}

void write_padding(std::ostream& out, std::uint64_t count)
{
    std::array<char, 4096> zeros{};
    while (count > 0) {
        const auto chunk = static_cast<std::streamsize>(std::min<std::uint64_t>(count, zeros.size()));
        out.write(zeros.data(), chunk);
        count -= static_cast<std::uint64_t>(chunk);
    }
}

void write_inner_plain(
    const std::filesystem::path& source_folder,
    const std::filesystem::path& output_path,
    std::string_view content_id,
    std::string_view passcode,
    bool encrypted_mode)
{
    auto files = enumerate_source_files(source_folder);
    const auto seed = deterministic_seed(content_id, passcode, files);

    std::uint64_t data_cursor = pfs_header_size + pfs_block_size;
    for (auto& file : files) {
        data_cursor = align_up(data_cursor, 16);
        file.image_offset = data_cursor;
        data_cursor += file.size;
    }
    const std::uint64_t image_size = align_up(data_cursor, pfs_block_size);
    const auto index = build_index(files, pfs_header_size + pfs_block_size);

    if (index.size() > pfs_block_size) {
        throw std::runtime_error("Native inner image index exceeded one PFS block.");
    }

    std::array<std::byte, pfs_header_size> header{};
    write_le64(header, 0x00, pfs_header_version_ps5);
    write_le64(header, 0x08, pfs_header_magic);
    header[0x1A] = std::byte{1};
    write_le16(header, pfs_mode_offset, static_cast<std::uint16_t>(
        pfs_mode_unknown_always_set | (encrypted_mode ? pfs_mode_encrypted : 0)));
    write_le32(header, pfs_block_size_offset, static_cast<std::uint32_t>(pfs_block_size));
    write_le64(header, 0x28, image_size / pfs_block_size);
    write_le64(header, 0x30, static_cast<std::uint64_t>(files.size() + 1u));
    write_le64(header, 0x38, image_size / pfs_block_size);
    write_le64(header, 0x40, 1);
    write_le32(header, pfs_unknown_index_offset, 1);
    std::copy(seed.begin(), seed.end(), header.begin() + static_cast<std::ptrdiff_t>(pfs_seed_offset));

    ensure_parent(output_path);
    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("Could not create inner image: " + output_path.string());
    }

    output.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    output.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size()));
    write_padding(output, pfs_block_size - index.size());

    std::uint64_t cursor = pfs_header_size + pfs_block_size;
    for (const auto& file : files) {
        if (file.image_offset > cursor) {
            write_padding(output, file.image_offset - cursor);
            cursor = file.image_offset;
        }
        copy_file_to_stream(file.full_path, output);
        cursor += file.size;
    }
    if (image_size > cursor) {
        write_padding(output, image_size - cursor);
    }
    if (!output) {
        throw std::runtime_error("Could not write inner image: " + output_path.string());
    }
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

void write_file_bytes(const std::filesystem::path& path, std::span<const std::byte> data)
{
    ensure_parent(path);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not create file: " + path.string());
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        throw std::runtime_error("Could not write file: " + path.string());
    }
}

void encrypt_inner_image_in_place(
    const std::filesystem::path& path,
    std::string_view content_id,
    std::string_view passcode)
{
    auto image = read_file_bytes(path);
    if (image.size() < pfs_header_size || (image.size() % pfs_inner_xts_sector_size) != 0) {
        throw std::runtime_error("Native inner image has invalid size for encryption.");
    }

    std::array<std::byte, 16> seed{};
    std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(pfs_seed_offset), seed.size(), seed.begin());
    const auto ekpfs = compute_package_key(content_id, passcode, 1, PackageKeyDigest::sha256);
    const auto keys = derive_pfs_encryption_keys(ekpfs, seed, false);
    write_le16(image, pfs_mode_offset, pfs_mode_unknown_always_set | pfs_mode_encrypted);

    const std::size_t start_sector = pfs_block_size / pfs_inner_xts_sector_size;
    const std::size_t total_sectors = image.size() / pfs_inner_xts_sector_size;
    for (std::size_t sector = start_sector; sector < total_sectors; ++sector) {
        auto unit = std::span<std::byte>(image).subspan(
            sector * pfs_inner_xts_sector_size,
            pfs_inner_xts_sector_size);
        xts_transform_unit(unit, keys.data_key, keys.tweak_key, sector, true);
    }

    write_file_bytes(path, image);
}

[[nodiscard]] std::string normalize_version(std::string_view version)
{
    if (version.size() == 5 &&
        version[0] >= '0' && version[0] <= '9' &&
        version[1] >= '0' && version[1] <= '9' &&
        version[2] == '.' &&
        version[3] >= '0' && version[3] <= '9' &&
        version[4] >= '0' && version[4] <= '9') {
        return std::string(version);
    }
    return "01.00";
}

[[nodiscard]] std::filesystem::path package_output_path(const PackageBuildOptions& options)
{
    std::string version = normalize_version(options.version);
    version.erase(std::remove(version.begin(), version.end(), '.'), version.end());
    while (version.size() < 4) {
        version.insert(version.begin(), '0');
    }
    const std::string name =
        options.content_id + "-A" + version.substr(0, 4) + "-V" + version.substr(0, 4) + ".pkg";
    return options.output_folder / name;
}

[[nodiscard]] std::string fallback_title_id(const PackageBuildOptions& options)
{
    if (is_valid_title_id(options.title_id)) {
        return options.title_id;
    }
    if (options.content_id.size() >= 16) {
        return options.content_id.substr(7, 9);
    }
    return "PPSA00000";
}

[[nodiscard]] std::string minimal_param_json(const PackageBuildOptions& options)
{
    const std::string title_id = fallback_title_id(options);
    const std::string title = options.title.empty() ? title_id : options.title;
    const std::string version = normalize_version(options.version);
    return "{\n"
           "  \"applicationCategoryType\": 0,\n"
           "  \"contentId\": \"" + options.content_id + "\",\n"
           "  \"contentVersion\": \"" + version + "\",\n"
           "  \"masterVersion\": \"" + version + "\",\n"
           "  \"requiredSystemSoftwareVersion\": \"00.00.00.00\",\n"
           "  \"titleId\": \"" + title_id + "\",\n"
           "  \"localizedParameters\": {\n"
           "    \"defaultLanguage\": \"en-US\",\n"
           "    \"en-US\": { \"titleName\": \"" + title + "\" }\n"
           "  }\n"
           "}\n";
}

[[nodiscard]] std::vector<std::byte> bytes_from_string(std::string_view text)
{
    std::vector<std::byte> out;
    out.reserve(text.size());
    for (char ch : text) {
        out.push_back(static_cast<std::byte>(static_cast<unsigned char>(ch)));
    }
    return out;
}

[[nodiscard]] std::vector<std::byte> param_json_bytes(const PackageBuildOptions& options)
{
    const auto path = options.source_folder / "sce_sys" / "param.json";
    if (std::filesystem::is_regular_file(path)) {
        return read_file_bytes(path);
    }
    return bytes_from_string(minimal_param_json(options));
}

[[nodiscard]] std::uint32_t content_type_for(BuildMode mode) noexcept
{
    switch (mode) {
    case BuildMode::additional_content_data:
        return content_type_additional_data;
    case BuildMode::additional_content_no_data:
        return content_type_additional_no_data;
    case BuildMode::application:
    case BuildMode::homebrew:
        return content_type_game;
    }
    return content_type_game;
}

[[nodiscard]] std::string build_manifest_json(
    const PackageBuildOptions& options,
    const std::vector<std::byte>& inner_image)
{
    const auto digest = sha256(inner_image);
    auto hex = [](std::span<const std::byte> data) {
        constexpr char digits[] = "0123456789abcdef";
        std::string out;
        out.reserve(data.size() * 2);
        for (std::byte byte : data) {
            const auto value = static_cast<unsigned char>(byte);
            out.push_back(digits[value >> 4u]);
            out.push_back(digits[value & 0x0Fu]);
        }
        return out;
    };

    return "{\n"
           "  \"builder\": \"LibProsperoPkg C++ native\",\n"
           "  \"contentId\": \"" + options.content_id + "\",\n"
           "  \"titleId\": \"" + fallback_title_id(options) + "\",\n"
           "  \"version\": \"" + normalize_version(options.version) + "\",\n"
           "  \"innerImageSize\": " + std::to_string(inner_image.size()) + ",\n"
           "  \"innerImageSha256\": \"" + hex(digest) + "\"\n"
           "}\n";
}

void write_fih_package(
    const std::filesystem::path& output_path,
    std::span<const std::byte> pfs_image,
    std::span<const std::byte> cnt)
{
    const std::uint64_t pfs_offset = PkgLayout::fih_header_region_size;
    const std::uint64_t cnt_offset = pfs_offset + align_up(pfs_image.size(), pfs_block_size);
    std::array<std::byte, PkgLayout::fih_header_region_size> header{};
    std::copy(PkgLayout::fih_magic.begin(), PkgLayout::fih_magic.end(), header.begin());
    header[PkgLayout::fih_signed_byte_offset] = std::byte{0};
    write_le64(header, PkgLayout::fih_pfs_image_offset_field, pfs_offset);
    write_le64(header, PkgLayout::fih_pfs_image_size_field, pfs_image.size());
    write_le64(header, PkgLayout::fih_embedded_cnt_offset_field, cnt_offset);
    write_le32(header, 0x90, static_cast<std::uint32_t>(align_up(pfs_image.size(), pfs_block_size) / pfs_block_size));
    write_le32(header, 0x94, 0);
    write_le32(header, 0x98, 0);
    write_le64(header, 0xA0, align_up(pfs_image.size(), pfs_block_size));

    ensure_parent(output_path);
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("Could not create FIH package: " + output_path.string());
    }
    out.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!pfs_image.empty()) {
        out.write(reinterpret_cast<const char*>(pfs_image.data()), static_cast<std::streamsize>(pfs_image.size()));
    }
    const std::uint64_t after_pfs = pfs_offset + pfs_image.size();
    if (cnt_offset > after_pfs) {
        write_padding(out, cnt_offset - after_pfs);
    }
    if (!cnt.empty()) {
        out.write(reinterpret_cast<const char*>(cnt.data()), static_cast<std::streamsize>(cnt.size()));
    }
    if (!out) {
        throw std::runtime_error("Could not write FIH package: " + output_path.string());
    }
}

[[nodiscard]] std::filesystem::path temp_path_near(
    const std::filesystem::path& directory,
    std::string_view suffix)
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return directory / (".libprosperopkg-" + std::to_string(ticks) + std::string(suffix));
}

void validate_common(std::string_view content_id, std::string_view passcode)
{
    if (!is_valid_content_id(content_id)) {
        throw std::invalid_argument("Content ID is not in the format XXYYYY-XXXXYYYYY_00-ZZZZZZZZZZZZZZZZ.");
    }
    if (passcode.size() != 32) {
        throw std::invalid_argument("Passcode must be exactly 32 characters.");
    }
}

} // namespace

std::filesystem::path build_inner_image(const InnerImageBuildOptions& options)
{
    validate_common(options.content_id, options.passcode);
    if (options.output_path.empty()) {
        throw std::invalid_argument("Inner image output path is empty.");
    }

    const bool encrypted = options.form == InnerImageForm::encrypted;
    write_inner_plain(options.source_folder, options.output_path, options.content_id, options.passcode, encrypted);

    if (encrypted) {
        encrypt_inner_image_in_place(options.output_path, options.content_id, options.passcode);
    } else if (options.form == InnerImageForm::compressed) {
        const auto raw_path = options.output_path;
        const auto pfsc_path = raw_path.string() + ".pfsc.tmp";
        pack_pfsc_zlib(raw_path, pfsc_path, 9, static_cast<std::uint32_t>(pfs_block_size));
        std::filesystem::remove(raw_path);
        std::filesystem::rename(pfsc_path, raw_path);
    } else if (options.form == InnerImageForm::kraken_compressed) {
        const auto raw_path = options.output_path;
        const auto pfsc_path = raw_path.string() + ".pfsv3.tmp";
        pack_pfsc_pfs_v3_stored(raw_path, pfsc_path, 7, 0x40000);
        std::filesystem::remove(raw_path);
        std::filesystem::rename(pfsc_path, raw_path);
    } else if (options.form != InnerImageForm::plaintext) {
        throw std::invalid_argument("Unknown inner image form.");
    }

    return options.output_path;
}

std::filesystem::path build_package(const PackageBuildOptions& options)
{
    validate_common(options.content_id, options.passcode);
    if (!std::filesystem::is_directory(options.source_folder)) {
        throw std::invalid_argument("Source folder does not exist: " + options.source_folder.string());
    }
    if (options.output_folder.empty()) {
        throw std::invalid_argument("Output folder is empty.");
    }

    std::filesystem::create_directories(options.output_folder);
    const auto output_path = package_output_path(options);
    const auto inner_tmp = temp_path_near(options.output_folder, ".pfs.tmp");

    InnerImageForm form = InnerImageForm::encrypted;
    if (options.inner_compression == InnerCompression::zlib) {
        form = InnerImageForm::compressed;
    } else if (options.inner_compression == InnerCompression::kraken) {
        form = InnerImageForm::kraken_compressed;
    }

    try {
        (void)build_inner_image(InnerImageBuildOptions{
            options.source_folder,
            inner_tmp,
            options.content_id,
            options.passcode,
            form});
        const auto inner = read_file_bytes(inner_tmp);

        PkgWriterOptions writer;
        writer.content_id = options.content_id;
        writer.flags = cnt_flags_ps5;
        writer.drm_type = cnt_drm_type_ps5;
        writer.content_type = content_type_for(options.mode);
        writer.entries.push_back(PkgWriterEntry{
            static_cast<std::uint32_t>(EntryId::param_json),
            "sce_sys/param.json",
            param_json_bytes(options),
            0,
            0});
        writer.entries.push_back(PkgWriterEntry{
            entry_id_native_inner_image,
            "pfs_image.dat",
            inner,
            0,
            0});
        writer.entries.push_back(PkgWriterEntry{
            entry_id_native_build_manifest,
            "libprosperopkg/build.json",
            bytes_from_string(build_manifest_json(options, inner)),
            0,
            0});

        const auto cnt = write_cnt(writer);
        if (options.output_format == BuildOutputFormat::metadata_container) {
            write_file_bytes(output_path, cnt);
        } else if (options.output_format == BuildOutputFormat::debug_image) {
            write_fih_package(output_path, inner, cnt);
        } else {
            throw std::invalid_argument("Unknown package output format.");
        }
    } catch (...) {
        std::filesystem::remove(inner_tmp);
        throw;
    }

    std::filesystem::remove(inner_tmp);
    return output_path;
}

} // namespace prosperopkg
