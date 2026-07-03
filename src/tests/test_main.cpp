// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/prosperopkg.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

struct TestFailure : std::runtime_error {
    using std::runtime_error::runtime_error;
};

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            throw TestFailure(std::string("CHECK failed: ") + #condition); \
        } \
    } while (false)

[[nodiscard]] std::vector<std::byte> bytes(std::string_view value)
{
    std::vector<std::byte> out;
    out.reserve(value.size());
    for (char ch : value) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

[[nodiscard]] int test_hex_value(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

template <std::size_t N>
[[nodiscard]] std::array<std::byte, N> fixed_bytes(std::string_view hex)
{
    CHECK(hex.size() == N * 2);
    std::array<std::byte, N> out{};
    for (std::size_t i = 0; i < N; ++i) {
        const int hi = test_hex_value(hex[i * 2]);
        const int lo = test_hex_value(hex[i * 2 + 1]);
        CHECK(hi >= 0);
        CHECK(lo >= 0);
        out[i] = static_cast<std::byte>((hi << 4) | lo);
    }
    return out;
}

void write_le64(std::vector<std::byte>& data, std::size_t offset, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i) {
        data[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xFFu);
    }
}

void write_le32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) {
        data[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xFFu);
    }
}

void write_le16(std::vector<std::byte>& data, std::size_t offset, std::uint16_t value)
{
    for (std::size_t i = 0; i < 2; ++i) {
        data[offset + i] = static_cast<std::byte>((value >> (i * 8u)) & 0xFFu);
    }
}

void write_be64(std::vector<std::byte>& data, std::size_t offset, std::uint64_t value)
{
    for (std::size_t i = 0; i < 8; ++i) {
        data[offset + i] = static_cast<std::byte>((value >> ((7u - i) * 8u)) & 0xFFu);
    }
}

void write_be32(std::vector<std::byte>& data, std::size_t offset, std::uint32_t value)
{
    for (std::size_t i = 0; i < 4; ++i) {
        data[offset + i] = static_cast<std::byte>((value >> ((3u - i) * 8u)) & 0xFFu);
    }
}

[[nodiscard]] std::uint16_t read_test_le16(const std::vector<std::byte>& data, std::size_t offset)
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[offset]) |
        (static_cast<std::uint16_t>(data[offset + 1]) << 8u));
}

[[nodiscard]] std::uint32_t read_test_le32(const std::vector<std::byte>& data, std::size_t offset)
{
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8u) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16u) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

[[nodiscard]] std::uint64_t read_test_le64(const std::vector<std::byte>& data, std::size_t offset)
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(data[offset + i]) << (i * 8u);
    }
    return value;
}

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    CHECK(size >= 0);
    file.seekg(0, std::ios::beg);
    std::vector<std::byte> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        CHECK(file.good());
    }
    return data;
}

[[nodiscard]] std::filesystem::path unique_temp_path(std::string_view stem, std::string_view extension = {})
{
    static std::uint64_t counter = 0;
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    std::string name(stem);
    name.push_back('-');
    name += std::to_string(ticks);
    name.push_back('-');
    name += std::to_string(counter++);
    name += extension;
    return std::filesystem::temp_directory_path() / name;
}

void write_file(const std::filesystem::path& path, std::span<const std::byte> data)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CHECK(file.good());
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    CHECK(file.good());
}

void write_text_file(const std::filesystem::path& path, std::string_view text)
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CHECK(file.good());
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    CHECK(file.good());
}

[[nodiscard]] std::filesystem::path make_build_source_tree()
{
    const auto root = unique_temp_path("prosperopkg-build-src");
    std::filesystem::create_directories(root / "sce_sys");
    std::filesystem::create_directories(root / "data" / "nested");
    write_text_file(
        root / "sce_sys" / "param.json",
        "{\"contentId\":\"UP9000-PPSA00000_00-PROSPERO00000000\",\"titleId\":\"PPSA00000\"}\n");
    write_text_file(root / "eboot.bin", "ELF placeholder\n");
    write_text_file(root / "data" / "nested" / "asset.txt", "native build fixture\n");
    write_text_file(root / "ignored.gp5", "should not be indexed\n");
    return root;
}

#ifdef _WIN32
[[nodiscard]] std::string quote_windows_arg(const std::string& arg)
{
    const auto needs_quotes = arg.empty() ||
        arg.find_first_of(" \t\n\v\"") != std::string::npos;
    if (!needs_quotes) {
        return arg;
    }
    std::string out = "\"";
    std::size_t backslashes = 0;
    for (char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
        } else if (ch == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            out.push_back(ch);
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}
#endif

// Runs an external program with a real argv vector, bypassing the shell so
// that quoted path arguments survive intact on Windows (cmd.exe's quote
// stripping would otherwise corrupt commands like: "a.exe" "C:\path\to.x").
// When stdout_file is non-null, the child's stdout is redirected to that
// file, replacing the Unix "> file" shell idiom.
[[nodiscard]] int run_program(const std::string& exe,
                              const std::vector<std::string>& args,
                              const std::filesystem::path* stdout_file = nullptr)
{
#ifdef _WIN32
    std::string command_line = quote_windows_arg(exe);
    for (const auto& arg : args) {
        command_line.push_back(' ');
        command_line += quote_windows_arg(arg);
    }
    std::vector<char> buffer(command_line.begin(), command_line.end());
    buffer.push_back('\0');

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HANDLE redirect = nullptr;
    if (stdout_file != nullptr) {
        redirect = CreateFileA(stdout_file->string().c_str(),
                               GENERIC_WRITE,
                               FILE_SHARE_READ,
                               &inheritable,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);
        if (redirect == INVALID_HANDLE_VALUE) {
            return -1;
        }
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = redirect;
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }

    PROCESS_INFORMATION info{};
    const BOOL ok = CreateProcessA(nullptr,
                                   buffer.data(),
                                   nullptr,
                                   nullptr,
                                   TRUE,
                                   0,
                                   nullptr,
                                   nullptr,
                                   &startup,
                                   &info);
    if (!ok) {
        if (redirect != nullptr) {
            CloseHandle(redirect);
        }
        return -1;
    }

    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exit_code = 1;
    GetExitCodeProcess(info.hProcess, &exit_code);
    CloseHandle(info.hProcess);
    CloseHandle(info.hThread);
    if (redirect != nullptr) {
        CloseHandle(redirect);
    }
    return static_cast<int>(exit_code);
#else
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        if (stdout_file != nullptr) {
            const int fd = ::open(stdout_file->c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC,
                                   0644);
            if (fd < 0) {
                std::_Exit(127);
            }
            ::dup2(fd, STDOUT_FILENO);
            ::close(fd);
        }
        std::vector<const char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(exe.c_str());
        for (const auto& arg : args) {
            argv.push_back(arg.c_str());
        }
        argv.push_back(nullptr);
        ::execv(exe.c_str(), const_cast<char* const*>(argv.data()));
        std::_Exit(127);
    }
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    return -1;
#endif
}

void expect_tool_usage_failure(const char* tool_path)
{
    CHECK(run_program(tool_path, {}) != 0);
}

template <typename Fn>
void expect_exception(Fn&& fn)
{
    bool thrown = false;
    try {
        fn();
    } catch (const std::exception&) {
        thrown = true;
    }
    CHECK(thrown);
}

[[nodiscard]] std::vector<std::byte> make_test_elf()
{
    std::vector<std::byte> elf(0x120);
    elf[0] = std::byte{0x7F};
    elf[1] = std::byte{'E'};
    elf[2] = std::byte{'L'};
    elf[3] = std::byte{'F'};
    elf[4] = std::byte{2};
    elf[5] = std::byte{1};
    elf[6] = std::byte{1};

    write_le16(elf, 0x10, 0x02);
    write_le16(elf, 0x12, 0x3E);
    write_le32(elf, 0x14, 1);
    write_le64(elf, 0x20, 0x40);
    write_le16(elf, 0x34, 0x40);
    write_le16(elf, 0x36, prosperopkg::SelfLayout::elf_phdr_size);
    write_le16(elf, 0x38, 1);

    const std::size_t ph = 0x40;
    write_le32(elf, ph + 0x00, 0x00000001);
    write_le32(elf, ph + 0x04, 0x00000005);
    write_le64(elf, ph + 0x08, 0x100);
    write_le64(elf, ph + 0x10, 0x400000);
    write_le64(elf, ph + 0x18, 0x400000);
    write_le64(elf, ph + 0x20, 0x10);
    write_le64(elf, ph + 0x28, 0x10);
    write_le64(elf, ph + 0x30, 0x10);

    for (std::size_t i = 0; i < 0x10; ++i) {
        elf[0x100 + i] = static_cast<std::byte>(0xA0 + i);
    }
    return elf;
}

[[nodiscard]] prosperopkg::PkgWriterOptions minimal_options()
{
    prosperopkg::PkgWriterOptions options;
    options.content_id = "UP9000-PPSA00000_00-PROSPERO00000000";
    options.flags = 0x01020304;
    options.drm_type = 1;
    options.content_type = 2;
    options.entries.push_back(prosperopkg::PkgWriterEntry{
        static_cast<std::uint32_t>(prosperopkg::EntryId::param_json),
        "sce_sys/param.json",
        bytes("{\"titleName\":\"Unit\"}"),
        0,
        0});
    options.entries.push_back(prosperopkg::PkgWriterEntry{
        static_cast<std::uint32_t>(prosperopkg::EntryId::icon0_png),
        "sce_sys/icon0.png",
        bytes("png"),
        prosperopkg::PkgLayout::entry_flag_encrypted,
        0x3000});
    return options;
}

void test_content_id()
{
    const std::string composed = prosperopkg::compose_content_id("up9", "ppsa1", "hello world!");
    CHECK(composed == "UP9000-PPSA10000_00-HELLOWORLD000000");
    CHECK(prosperopkg::is_valid_content_id("UP9000-PPSA00000_00-PROSPERO00000000"));
    CHECK(!prosperopkg::is_valid_content_id("up9000-PPSA00000_00-PROSPERO00000000"));
    CHECK(prosperopkg::is_valid_title_id("PPSA00000"));
    CHECK(!prosperopkg::is_valid_title_id("PPSA0000"));
    CHECK(prosperopkg::is_dlc_mode(prosperopkg::PackageMode::additional_content_data));
    CHECK(!prosperopkg::is_dlc_mode(prosperopkg::PackageMode::application));
}

void test_crc32c()
{
    constexpr std::string_view sample = "123456789";
    CHECK(prosperopkg::crc32c_bytes(sample.data(), sample.size()) == 0xE3069283u);
}

void test_sha256()
{
    constexpr std::string_view sample = "abc";
    const auto digest = prosperopkg::sha256(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(sample.data()), sample.size()));
    const std::array<std::byte, 32> expected{
        std::byte{0xBA}, std::byte{0x78}, std::byte{0x16}, std::byte{0xBF},
        std::byte{0x8F}, std::byte{0x01}, std::byte{0xCF}, std::byte{0xEA},
        std::byte{0x41}, std::byte{0x41}, std::byte{0x40}, std::byte{0xDE},
        std::byte{0x5D}, std::byte{0xAE}, std::byte{0x22}, std::byte{0x23},
        std::byte{0xB0}, std::byte{0x03}, std::byte{0x61}, std::byte{0xA3},
        std::byte{0x96}, std::byte{0x17}, std::byte{0x7A}, std::byte{0x9C},
        std::byte{0xB4}, std::byte{0x10}, std::byte{0xFF}, std::byte{0x61},
        std::byte{0xF2}, std::byte{0x00}, std::byte{0x15}, std::byte{0xAD},
    };
    CHECK(digest == expected);
}

void test_sha3_256()
{
    const auto empty = prosperopkg::sha3_256({});
    CHECK(empty == fixed_bytes<32>("a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a"));

    constexpr std::string_view sample = "abc";
    const auto digest = prosperopkg::sha3_256(
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(sample.data()), sample.size()));
    CHECK(digest == fixed_bytes<32>("3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"));
}

void test_hmac_sha256()
{
    std::array<std::byte, 20> key{};
    key.fill(std::byte{0x0B});
    constexpr std::string_view data = "Hi There";
    const auto digest = prosperopkg::hmac_sha256(
        key,
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(data.data()), data.size()));
    CHECK(digest == fixed_bytes<32>("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
}

void test_aes128_block()
{
    const auto key = fixed_bytes<16>("000102030405060708090a0b0c0d0e0f");
    const auto plain = fixed_bytes<16>("00112233445566778899aabbccddeeff");
    const auto cipher = fixed_bytes<16>("69c4e0d86a7b0430d8cdb78070b4c55a");

    CHECK(prosperopkg::aes128_encrypt_block(key, plain) == cipher);
    CHECK(prosperopkg::aes128_decrypt_block(key, cipher) == plain);
}

void test_xts_transform_unit()
{
    const auto data_key = fixed_bytes<16>("000102030405060708090a0b0c0d0e0f");
    const auto tweak_key = fixed_bytes<16>("101112131415161718191a1b1c1d1e1f");
    const auto plain = fixed_bytes<32>("00112233445566778899aabbccddeeff102132435465768798a9bacbdcedfe0f");
    const auto expected = fixed_bytes<32>("8f7fa126002787b7e04a2b11836e7a558e215d08e979ff383bfee49c565cd53f");

    std::vector<std::byte> unit(plain.begin(), plain.end());
    prosperopkg::xts_transform_unit(unit, data_key, tweak_key, 0x0123456789ABCDEFull, true);
    CHECK(std::equal(unit.begin(), unit.end(), expected.begin(), expected.end()));
    prosperopkg::xts_transform_unit(unit, data_key, tweak_key, 0x0123456789ABCDEFull, false);
    CHECK(std::equal(unit.begin(), unit.end(), plain.begin(), plain.end()));
}

void test_pfs_keys()
{
    constexpr std::string_view content_id = "UP9000-PPSA00000_00-PROSPERO00000000";
    constexpr std::string_view passcode = "00000000000000000000000000000000";
    const auto seed = fixed_bytes<16>("000102030405060708090a0b0c0d0e0f");

    CHECK(prosperopkg::compute_package_key(content_id, passcode, 1) ==
          fixed_bytes<32>("659f2e5ecd67fb78f2eb96f96a5127b853baf56e27a36f82411064de4370b22e"));

    const auto ekpfs = prosperopkg::derive_ekpfs(content_id, passcode);
    CHECK(ekpfs == fixed_bytes<32>("d2a92aa1153b6c73ee56ec279cb5bd4821d1215e97bea5995cc93443bdc2aae9"));

    const auto classic = prosperopkg::derive_pfs_encryption_keys(ekpfs, seed);
    CHECK(classic.tweak_key == fixed_bytes<16>("f9a0660453251c4f2991d016aa2c2ea9"));
    CHECK(classic.data_key == fixed_bytes<16>("07236321e00e02d7560a2d36b8a1df9b"));
    CHECK(prosperopkg::derive_pfs_sign_key(ekpfs, seed) ==
          fixed_bytes<32>("13a62652daac56f696236069bb7d8b836c42784b7ac038c14aea273559759a3f"));

    const auto image = prosperopkg::derive_image_encryption_keys(ekpfs, seed);
    CHECK(image.tweak_key == fixed_bytes<16>("b3f7f686e56b0f01f091418e0f684b8e"));
    CHECK(image.data_key == fixed_bytes<16>("4bca2b69b4e1098e22de2c28a754ff34"));
    CHECK(prosperopkg::derive_image_sign_key(ekpfs, seed) ==
          fixed_bytes<32>("c527a73dfce9e17afa13b2ef57ce1411e4cda5b71969416a6097c6b4311a16c6"));
}

void test_pfs_image_transforms()
{
    prosperopkg::PfsEncryptionKeys keys;
    keys.tweak_key = fixed_bytes<16>("101112131415161718191a1b1c1d1e1f");
    keys.data_key = fixed_bytes<16>("000102030405060708090a0b0c0d0e0f");

    std::vector<std::byte> inner(0x3000);
    for (std::size_t i = 0; i < inner.size(); ++i) {
        inner[i] = static_cast<std::byte>((i * 3u + 7u) & 0xFFu);
    }
    const auto original_inner = inner;
    CHECK(prosperopkg::transform_inner_pfs_image(inner, keys, true, 0x1000) == 2u);
    CHECK(std::equal(inner.begin(), inner.begin() + 0x1000, original_inner.begin(), original_inner.begin() + 0x1000));
    CHECK(!std::equal(inner.begin() + 0x1000, inner.end(), original_inner.begin() + 0x1000, original_inner.end()));
    CHECK(prosperopkg::transform_inner_pfs_image(inner, keys, false, 0x1000) == 2u);
    CHECK(inner == original_inner);

    CHECK(prosperopkg::outer_pfs_metadata_block_index(0x10000, 0x70000) == 6u);
    CHECK(prosperopkg::outer_pfs_block_sector(2, prosperopkg::OuterPfsBlockKind::signed_block) ==
          (prosperopkg::pfs_outer_signed_block_tweak_flag | 2u));

    std::vector<std::byte> outer(32 * 3);
    for (std::size_t i = 0; i < 32; ++i) {
        outer[i] = static_cast<std::byte>(i);
        outer[32 + i] = static_cast<std::byte>(0x80u + i);
        outer[64 + i] = static_cast<std::byte>(i);
    }
    const auto original_outer = outer;
    const std::array<prosperopkg::OuterPfsBlockKind, 3> kinds{
        prosperopkg::OuterPfsBlockKind::data,
        prosperopkg::OuterPfsBlockKind::plaintext,
        prosperopkg::OuterPfsBlockKind::signed_block,
    };
    CHECK(prosperopkg::transform_outer_pfs_image(outer, keys, 32, kinds, true) == 2u);
    CHECK(!std::equal(outer.begin(), outer.begin() + 32, original_outer.begin(), original_outer.begin() + 32));
    CHECK(std::equal(outer.begin() + 32, outer.begin() + 64, original_outer.begin() + 32, original_outer.begin() + 64));
    CHECK(!std::equal(outer.begin() + 64, outer.end(), original_outer.begin() + 64, original_outer.end()));
    CHECK(!std::equal(outer.begin(), outer.begin() + 32, outer.begin() + 64, outer.end()));
    CHECK(prosperopkg::transform_outer_pfs_image(outer, keys, 32, kinds, false) == 2u);
    CHECK(outer == original_outer);
}

void test_pfs_signature()
{
    std::vector<std::byte> block(0x100);
    for (std::size_t i = 0; i < block.size(); ++i) {
        block[i] = static_cast<std::byte>((i * 11u + 3u) & 0xFFu);
    }

    const auto block_hash = prosperopkg::compute_outer_pfs_block_hash(block);
    CHECK(block_hash == prosperopkg::sha3_256(block));

    std::vector<std::byte> superblock(0x600);
    for (std::size_t i = 0; i < superblock.size(); ++i) {
        superblock[i] = static_cast<std::byte>((i * 5u + 0x41u) & 0xFFu);
    }
    std::fill_n(
        superblock.begin() + static_cast<std::ptrdiff_t>(prosperopkg::pfs_superblock_icv_offset),
        prosperopkg::pfs_signature_hash_length,
        std::byte{0xCC});

    auto manual_region = superblock;
    std::fill_n(
        manual_region.begin() + static_cast<std::ptrdiff_t>(prosperopkg::pfs_superblock_icv_offset),
        prosperopkg::pfs_signature_hash_length,
        std::byte{0});
    manual_region.resize(prosperopkg::pfs_superblock_icv_coverage);
    const auto expected_icv = prosperopkg::sha3_256(manual_region);
    CHECK(prosperopkg::compute_superblock_icv(superblock) == expected_icv);

    prosperopkg::write_superblock_icv(superblock);
    CHECK(std::equal(
        expected_icv.begin(),
        expected_icv.end(),
        superblock.begin() + static_cast<std::ptrdiff_t>(prosperopkg::pfs_superblock_icv_offset)));
    CHECK(prosperopkg::compute_superblock_icv(superblock) == expected_icv);

    prosperopkg::write_superblock_root_hash_for_block(superblock, block, 0x11223344u);
    CHECK(std::equal(
        block_hash.begin(),
        block_hash.end(),
        superblock.begin() + static_cast<std::ptrdiff_t>(prosperopkg::pfs_superblock_root_hash_offset)));
    const auto idx = prosperopkg::pfs_superblock_root_block_index_offset;
    CHECK(superblock[idx + 0] == std::byte{0x44});
    CHECK(superblock[idx + 1] == std::byte{0x33});
    CHECK(superblock[idx + 2] == std::byte{0x22});
    CHECK(superblock[idx + 3] == std::byte{0x11});
}

void test_image_digests()
{
    std::vector<std::byte> superblock(prosperopkg::image_digest_block_size);
    for (std::size_t i = 0; i < superblock.size(); ++i) {
        superblock[i] = static_cast<std::byte>((i * 7u + 0x22u) & 0xFFu);
    }
    write_le64(superblock, 0, 2);
    superblock[8] = std::byte{0x0B};
    superblock[9] = std::byte{0x2A};
    superblock[10] = std::byte{0x33};
    superblock[11] = std::byte{0x01};

    const auto superblock_hash = prosperopkg::sha3_256(superblock);
    CHECK(prosperopkg::compute_sblock_digest(superblock) == superblock_hash);
    CHECK(prosperopkg::compute_game_digest(superblock) == superblock_hash);
    CHECK(prosperopkg::compute_fixed_info_digest(superblock) == superblock_hash);
    const auto body_payload = bytes("body");
    const auto entry_payload = bytes("entry");
    CHECK(prosperopkg::compute_body_digest(body_payload) == prosperopkg::sha3_256(body_payload));
    CHECK(prosperopkg::compute_entry_digest(entry_payload) == prosperopkg::sha3_256(entry_payload));

    std::array<std::byte, prosperopkg::content_descriptor_size> content_descriptor{};
    for (std::size_t i = 0; i < content_descriptor.size(); ++i) {
        content_descriptor[i] = static_cast<std::byte>(0x30u + i);
    }
    const auto game_digest = prosperopkg::sha3_256(bytes("game"));
    std::array<std::byte, prosperopkg::image_digest_size> major_digest{};
    std::vector<std::byte> content_preimage(content_descriptor.begin(), content_descriptor.end());
    content_preimage.insert(content_preimage.end(), game_digest.begin(), game_digest.end());
    content_preimage.insert(content_preimage.end(), major_digest.begin(), major_digest.end());
    CHECK(prosperopkg::compute_content_digest(content_descriptor, game_digest, major_digest, true) ==
          prosperopkg::sha3_256(content_preimage));

    std::vector<std::byte> content_no_game(content_descriptor.begin(), content_descriptor.end());
    content_no_game.insert(content_no_game.end(), major_digest.begin(), major_digest.end());
    CHECK(prosperopkg::compute_content_digest(content_descriptor, std::span<const std::byte>{}, major_digest, false) ==
          prosperopkg::sha3_256(content_no_game));

    std::array<std::byte, prosperopkg::header_digest_prefix_size> prefix{};
    std::array<std::byte, prosperopkg::header_digest_mount_descriptor_size> mount{};
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        prefix[i] = static_cast<std::byte>(i);
    }
    for (std::size_t i = 0; i < mount.size(); ++i) {
        mount[i] = static_cast<std::byte>(0x80u + i);
    }
    const auto patched_mount = prosperopkg::force_fih_relative_image_offset(mount);
    CHECK(patched_mount[0x10] == std::byte{0x00});
    CHECK(patched_mount[0x11] == std::byte{0x00});
    CHECK(patched_mount[0x12] == std::byte{0x00});
    CHECK(patched_mount[0x13] == std::byte{0x00});
    CHECK(patched_mount[0x14] == std::byte{0x00});
    CHECK(patched_mount[0x15] == std::byte{0x01});
    CHECK(patched_mount[0x16] == std::byte{0x00});
    CHECK(patched_mount[0x17] == std::byte{0x00});

    std::vector<std::byte> header_preimage(prefix.begin(), prefix.end());
    header_preimage.insert(header_preimage.end(), patched_mount.begin(), patched_mount.end());
    CHECK(prosperopkg::compute_header_digest(prefix, patched_mount) == prosperopkg::sha3_256(header_preimage));

    const auto one_payload = bytes("one");
    const auto two_payload = bytes("two");
    std::array<std::array<std::byte, prosperopkg::image_digest_size>, 2> digests{
        prosperopkg::sha3_256(one_payload),
        prosperopkg::sha3_256(two_payload),
    };
    std::vector<std::byte> concat_preimage(digests[0].begin(), digests[0].end());
    concat_preimage.insert(concat_preimage.end(), digests[1].begin(), digests[1].end());
    CHECK(prosperopkg::compute_concat_digest(digests) == prosperopkg::sha3_256(concat_preimage));

    const auto payload_a = bytes("alpha");
    const auto payload_self = bytes("self");
    const auto payload_c = bytes("charlie");
    const std::array<prosperopkg::CntDigestPayload, 3> entries{
        prosperopkg::CntDigestPayload{0x1000, payload_a},
        prosperopkg::CntDigestPayload{prosperopkg::digest_table_entry_id, payload_self},
        prosperopkg::CntDigestPayload{0x1200, payload_c},
    };
    const auto table = prosperopkg::build_entry_digest_table(entries);
    CHECK(table.size() == prosperopkg::image_digest_size * entries.size());
    const auto digest_a = prosperopkg::sha3_256(payload_a);
    const auto digest_c = prosperopkg::sha3_256(payload_c);
    CHECK(std::equal(digest_a.begin(), digest_a.end(), table.begin()));
    CHECK(std::all_of(
        table.begin() + static_cast<std::ptrdiff_t>(prosperopkg::image_digest_size),
        table.begin() + static_cast<std::ptrdiff_t>(prosperopkg::image_digest_size * 2),
        [](std::byte b) { return b == std::byte{0}; }));
    CHECK(std::equal(
        digest_c.begin(),
        digest_c.end(),
        table.begin() + static_cast<std::ptrdiff_t>(prosperopkg::image_digest_size * 2)));

    std::vector<std::byte> cnt(0x1100);
    for (std::size_t i = 0; i < cnt.size(); ++i) {
        cnt[i] = static_cast<std::byte>((i * 13u + 1u) & 0xFFu);
    }
    CHECK(prosperopkg::compute_package_digest(cnt) ==
          prosperopkg::sha3_256(std::span<const std::byte>(cnt).first(prosperopkg::package_digest_region_size)));

    write_be32(cnt, 0x1C, 0x40);
    write_be64(cnt, 0x20, 0x80);
    CHECK(prosperopkg::compute_cnt_header_rollup_digest(cnt) ==
          prosperopkg::sha3_256(std::span<const std::byte>(cnt).subspan(0x80, 0x40)));

    std::vector<std::byte> image(prosperopkg::image_digest_block_size * 3);
    std::copy(superblock.begin(), superblock.end(), image.begin() + prosperopkg::image_digest_block_size);
    const auto found = prosperopkg::locate_superblock(image);
    CHECK(found.has_value());
    CHECK(*found == prosperopkg::image_digest_block_size);
    const auto digest_from_image = prosperopkg::compute_sblock_digest_from_image(image);
    CHECK(digest_from_image.has_value());
    CHECK(digest_from_image->offset == prosperopkg::image_digest_block_size);
    CHECK(digest_from_image->digest == superblock_hash);
}

void test_cnt_writer_reader_roundtrip()
{
    const auto image = prosperopkg::write_cnt(minimal_options());
    std::string backing(reinterpret_cast<const char*>(image.data()), image.size());
    std::istringstream stream(backing, std::ios::binary);

    const auto type = prosperopkg::detect_type(stream);
    CHECK(type.has_value());
    CHECK(*type == prosperopkg::PackageType::meta);

    stream.clear();
    stream.seekg(0, std::ios::beg);
    const auto pkg = prosperopkg::read_pkg(stream);
    CHECK(pkg.type == prosperopkg::PackageType::meta);
    CHECK(pkg.header.has_value());
    CHECK(pkg.header->flags == 0x01020304u);
    CHECK(pkg.header->entry_count == 3u);
    CHECK(pkg.header->content_id == "UP9000-PPSA00000_00-PROSPERO00000000");
    CHECK(pkg.entries.size() == 3u);
    CHECK(pkg.entries[0].id == prosperopkg::EntryId::param_json);
    CHECK(pkg.entries[0].name == "sce_sys/param.json");
    CHECK(pkg.entries[1].encrypted());
    CHECK(pkg.entries[1].key_index() == 3u);
    CHECK(pkg.entries[1].name == "sce_sys/icon0.png");
    CHECK(pkg.entries[2].id == prosperopkg::EntryId::entry_names);
}

void test_fih_embedded_cnt_reader()
{
    const auto cnt = prosperopkg::write_cnt(minimal_options());
    std::vector<std::byte> image(prosperopkg::PkgLayout::fih_header_region_size + cnt.size());
    std::copy(
        prosperopkg::PkgLayout::fih_magic.begin(),
        prosperopkg::PkgLayout::fih_magic.end(),
        image.begin());
    image[prosperopkg::PkgLayout::fih_signed_byte_offset] = std::byte{0x00};
    write_le64(image, prosperopkg::PkgLayout::fih_pfs_image_offset_field, 0x10000);
    write_le64(image, prosperopkg::PkgLayout::fih_pfs_image_size_field, 0);
    write_le64(
        image,
        prosperopkg::PkgLayout::fih_embedded_cnt_offset_field,
        prosperopkg::PkgLayout::fih_header_region_size);
    std::copy(cnt.begin(), cnt.end(), image.begin() + prosperopkg::PkgLayout::fih_header_region_size);

    std::string backing(reinterpret_cast<const char*>(image.data()), image.size());
    std::istringstream stream(backing, std::ios::binary);
    const auto pkg = prosperopkg::read_pkg(stream);
    CHECK(pkg.type == prosperopkg::PackageType::full_debug);
    CHECK(pkg.fih.has_value());
    CHECK(pkg.fih->signed_byte == 0x00u);
    CHECK(pkg.header.has_value());
    CHECK(pkg.header->content_id == "UP9000-PPSA00000_00-PROSPERO00000000");
    CHECK(pkg.entries.size() == 3u);
}

void test_native_inner_image_builder()
{
    const auto source = make_build_source_tree();
    const auto plain = unique_temp_path("prosperopkg-inner", ".img");
    const auto encrypted = unique_temp_path("prosperopkg-inner", ".enc");
    const auto compressed = unique_temp_path("prosperopkg-inner", ".pfsc");
    const auto kraken = unique_temp_path("prosperopkg-inner", ".pfsv3");
    const auto unpacked = unique_temp_path("prosperopkg-inner", ".unpacked");
    const auto kraken_unpacked = unique_temp_path("prosperopkg-inner", ".kraken-unpacked");

    const std::string content_id = "UP9000-PPSA00000_00-PROSPERO00000000";
    const std::string passcode = "00000000000000000000000000000000";

    CHECK(prosperopkg::build_inner_image(prosperopkg::InnerImageBuildOptions{
              source,
              plain,
              content_id,
              passcode,
              prosperopkg::InnerImageForm::plaintext}) == plain);
    const auto plain_bytes = read_file(plain);
    CHECK(plain_bytes.size() >= 0x20000u);
    CHECK(read_test_le64(plain_bytes, 0x00) == 2u);
    CHECK(read_test_le64(plain_bytes, 0x08) == 20130315u);
    CHECK((read_test_le16(plain_bytes, 0x1C) & 0x4u) == 0u);
    const std::string plain_text(reinterpret_cast<const char*>(plain_bytes.data()), plain_bytes.size());
    CHECK(plain_text.find("LPFSIDX1") != std::string::npos);
    CHECK(plain_text.find("data/nested/asset.txt") != std::string::npos);
    CHECK(plain_text.find("ignored.gp5") == std::string::npos);

    CHECK(prosperopkg::build_inner_image(prosperopkg::InnerImageBuildOptions{
              source,
              encrypted,
              content_id,
              passcode,
              prosperopkg::InnerImageForm::encrypted}) == encrypted);
    const auto encrypted_bytes = read_file(encrypted);
    CHECK(encrypted_bytes.size() == plain_bytes.size());
    CHECK((read_test_le16(encrypted_bytes, 0x1C) & 0x4u) != 0u);
    CHECK(!std::equal(
        encrypted_bytes.begin() + 0x10000,
        encrypted_bytes.begin() + 0x11000,
        plain_bytes.begin() + 0x10000));

    CHECK(prosperopkg::build_inner_image(prosperopkg::InnerImageBuildOptions{
              source,
              compressed,
              content_id,
              passcode,
              prosperopkg::InnerImageForm::compressed}) == compressed);
    CHECK(prosperopkg::is_pfsc_file(compressed));
    CHECK(prosperopkg::unpack_pfsc(compressed, unpacked) == plain_bytes.size());
    const auto unpacked_bytes = read_file(unpacked);
    CHECK(read_test_le64(unpacked_bytes, 0x00) == 2u);
    CHECK(read_test_le64(unpacked_bytes, 0x08) == 20130315u);

    CHECK(prosperopkg::build_inner_image(prosperopkg::InnerImageBuildOptions{
              source,
              kraken,
              content_id,
              passcode,
              prosperopkg::InnerImageForm::kraken_compressed}) == kraken);
    CHECK(prosperopkg::is_pfsc_file(kraken));
    const auto kraken_bytes = read_file(kraken);
    CHECK(read_test_le16(kraken_bytes, 0x04) == 3u);
    CHECK(read_test_le32(kraken_bytes, 0x08) == 0x40000u);
    CHECK(read_test_le64(kraken_bytes, 0x18) == plain_bytes.size());
    CHECK(prosperopkg::unpack_pfsc(kraken, kraken_unpacked) == plain_bytes.size());
    CHECK(read_file(kraken_unpacked) == plain_bytes);

    std::filesystem::remove_all(source);
    std::filesystem::remove(plain);
    std::filesystem::remove(encrypted);
    std::filesystem::remove(compressed);
    std::filesystem::remove(kraken);
    std::filesystem::remove(unpacked);
    std::filesystem::remove(kraken_unpacked);
}

void test_native_package_builder()
{
    const auto source = make_build_source_tree();
    const auto meta_dir = unique_temp_path("prosperopkg-meta-out");
    const auto fih_dir = unique_temp_path("prosperopkg-fih-out");

    prosperopkg::PackageBuildOptions options;
    options.source_folder = source;
    options.output_folder = meta_dir;
    options.content_id = "UP9000-PPSA00000_00-PROSPERO00000000";
    options.passcode = "00000000000000000000000000000000";
    options.title = "Native Fixture";
    options.title_id = "PPSA00000";
    options.version = "01.23";
    options.output_format = prosperopkg::BuildOutputFormat::metadata_container;

    const auto meta_path = prosperopkg::build_package(options);
    CHECK(std::filesystem::exists(meta_path));
    const auto meta_pkg = prosperopkg::read_pkg(meta_path);
    CHECK(meta_pkg.type == prosperopkg::PackageType::meta);
    CHECK(meta_pkg.header.has_value());
    CHECK(meta_pkg.header->content_id == options.content_id);
    CHECK(std::any_of(meta_pkg.entries.begin(), meta_pkg.entries.end(), [](const prosperopkg::PkgEntry& entry) {
        return entry.name == "pfs_image.dat";
    }));
    CHECK(std::any_of(meta_pkg.entries.begin(), meta_pkg.entries.end(), [](const prosperopkg::PkgEntry& entry) {
        return entry.name == "libprosperopkg/build.json";
    }));

    options.output_folder = fih_dir;
    options.output_format = prosperopkg::BuildOutputFormat::debug_image;
    options.inner_compression = prosperopkg::InnerCompression::zlib;
    const auto fih_path = prosperopkg::build_package(options);
    CHECK(std::filesystem::exists(fih_path));
    const auto fih_pkg = prosperopkg::read_pkg(fih_path);
    CHECK(fih_pkg.type == prosperopkg::PackageType::full_debug);
    CHECK(fih_pkg.fih.has_value());
    CHECK(fih_pkg.fih->signed_byte == 0x00u);
    CHECK(fih_pkg.fih->pfs_image_offset == prosperopkg::PkgLayout::fih_header_region_size);
    CHECK(fih_pkg.fih->pfs_image_size > 0);
    CHECK(fih_pkg.header.has_value());
    CHECK(fih_pkg.header->content_id == options.content_id);

    std::filesystem::remove_all(source);
    std::filesystem::remove_all(meta_dir);
    std::filesystem::remove_all(fih_dir);
}

void test_ucp_build_read_verify_repair()
{
    std::vector<prosperopkg::UcpEntry> entries{
        {"zeta.txt", bytes("last")},
        {"alpha.txt", bytes("first")},
    };

    const auto archive = prosperopkg::build_ucp(entries);
    CHECK(prosperopkg::is_ucp(archive));
    CHECK(prosperopkg::validate_ucp(archive));
    CHECK(prosperopkg::verify_ucp_digest(archive));

    const auto read = prosperopkg::read_ucp(archive);
    CHECK(read.size() == 2u);
    CHECK(read[0].name == "alpha.txt");
    CHECK(read[0].data == bytes("first"));
    CHECK(read[1].name == "zeta.txt");
    CHECK(read[1].data == bytes("last"));

    auto broken = archive;
    broken[prosperopkg::UcpLayout::digest_offset] = std::byte{0};
    broken[prosperopkg::UcpLayout::digest_offset + 1] = std::byte{0};
    CHECK(!prosperopkg::verify_ucp_digest(broken));

    const auto repaired = prosperopkg::repair_ucp_digest(broken);
    CHECK(prosperopkg::verify_ucp_digest(repaired));
    CHECK(prosperopkg::read_ucp(repaired)[0].name == "alpha.txt");
}

void test_pfsc_raw_roundtrip()
{
    const std::filesystem::path raw_path = unique_temp_path("prosperopkg-pfsc-raw", ".bin");
    const std::filesystem::path pfsc_path = unique_temp_path("prosperopkg-pfsc-raw", ".pfsc");
    const std::filesystem::path out_path = unique_temp_path("prosperopkg-pfsc-raw", ".out");

    std::vector<std::byte> raw(0x3000);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::byte>((i * 17u + 0x55u) & 0xFFu);
    }
    write_file(raw_path, raw);

    prosperopkg::pack_pfsc_raw(raw_path, pfsc_path, 0x1000);
    CHECK(prosperopkg::is_pfsc_file(pfsc_path));
    const auto info = prosperopkg::read_pfsc_info(pfsc_path);
    CHECK(info.block_size == 0x1000u);
    CHECK(info.data_length == raw.size());
    CHECK(info.block_count == 3u);

    CHECK(prosperopkg::unpack_pfsc(pfsc_path, out_path) == raw.size());
    CHECK(read_file(out_path) == raw);

    std::filesystem::remove(raw_path);
    std::filesystem::remove(pfsc_path);
    std::filesystem::remove(out_path);
}

void test_pfsc_zlib_roundtrip()
{
    const std::filesystem::path raw_path = unique_temp_path("prosperopkg-pfsc-zlib", ".bin");
    const std::filesystem::path pfsc_path = unique_temp_path("prosperopkg-pfsc-zlib", ".pfsc");
    const std::filesystem::path out_path = unique_temp_path("prosperopkg-pfsc-zlib", ".out");

    std::vector<std::byte> raw(0x40000);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::byte>((i / 128u) & 0x0Fu);
    }
    write_file(raw_path, raw);

    prosperopkg::pack_pfsc_zlib(raw_path, pfsc_path, 9, 0x10000);
    CHECK(prosperopkg::is_pfsc_file(pfsc_path));
    const auto info = prosperopkg::read_pfsc_info(pfsc_path);
    CHECK(info.block_size == 0x10000u);
    CHECK(info.data_length == raw.size());
    CHECK(info.block_count == 4u);
#ifdef PROSPEROPKG_HAS_ZLIB
    CHECK(std::filesystem::file_size(pfsc_path) < raw.size());
#endif

    CHECK(prosperopkg::unpack_pfsc(pfsc_path, out_path) == raw.size());
    CHECK(read_file(out_path) == raw);

    std::filesystem::remove(raw_path);
    std::filesystem::remove(pfsc_path);
    std::filesystem::remove(out_path);
}

void test_pfsc_pfs_v3_stored_roundtrip()
{
    const std::filesystem::path raw_path = unique_temp_path("prosperopkg-pfsv3", ".bin");
    const std::filesystem::path pfsc_path = unique_temp_path("prosperopkg-pfsv3", ".pfsc");
    const std::filesystem::path out_path = unique_temp_path("prosperopkg-pfsv3", ".out");

    std::vector<std::byte> raw(0x41021);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<std::byte>((i * 29u + 0x33u) & 0xFFu);
    }
    write_file(raw_path, raw);

    prosperopkg::pack_pfsc_pfs_v3_stored(raw_path, pfsc_path, 7, 0x40000);
    CHECK(prosperopkg::is_pfsc_file(pfsc_path));
    const auto pfsc = read_file(pfsc_path);
    CHECK(read_test_le16(pfsc, 0x04) == 3u);
    CHECK(read_test_le16(pfsc, 0x06) == 7u);
    CHECK(read_test_le32(pfsc, 0x08) == 0x40000u);
    CHECK(read_test_le64(pfsc, 0x18) == raw.size());
    const auto info = prosperopkg::read_pfsc_info(pfsc_path);
    CHECK(info.block_size == 0x40000u);
    CHECK(info.data_length == raw.size());
    CHECK(info.block_count == 2u);

    CHECK(prosperopkg::unpack_pfsc(pfsc_path, out_path) == raw.size());
    CHECK(read_file(out_path) == raw);

    std::filesystem::remove(raw_path);
    std::filesystem::remove(pfsc_path);
    std::filesystem::remove(out_path);
}

void test_lzn_roundtrip()
{
    const auto empty = prosperopkg::lzn_compress({});
    CHECK(prosperopkg::is_lzn_frame(empty));
    CHECK(prosperopkg::read_lzn_frame_info(empty).stored_raw());
    CHECK(prosperopkg::lzn_decompress(empty).empty());

    constexpr std::string_view phrase = "LibProsperoPkg LZN1 clean-room fixture. ";
    std::vector<std::byte> repetitive;
    repetitive.reserve(0x20000);
    while (repetitive.size() < 0x20000u) {
        for (const char ch : phrase) {
            repetitive.push_back(static_cast<std::byte>(ch));
        }
    }
    repetitive.resize(0x20000);

    const auto compressed = prosperopkg::lzn_compress(repetitive, 2);
    const auto info = prosperopkg::read_lzn_frame_info(compressed);
    CHECK(info.version == 1u);
    CHECK(!info.stored_raw());
    CHECK(compressed.size() < repetitive.size() / 4u);
    CHECK(prosperopkg::lzn_decompress(compressed) == repetitive);

    std::vector<std::byte> decoded(repetitive.size());
    CHECK(prosperopkg::lzn_decompress_to(compressed, decoded) == repetitive.size());
    CHECK(decoded == repetitive);

    std::vector<std::byte> too_small(repetitive.size() - 1u);
    expect_exception([&] { (void)prosperopkg::lzn_decompress_to(compressed, too_small); });

    std::vector<std::byte> randomish(0x4000);
    for (std::size_t i = 0; i < randomish.size(); ++i) {
        randomish[i] = static_cast<std::byte>(((i * 131u) ^ (i >> 3u) ^ 0x5Au) & 0xFFu);
    }
    CHECK(prosperopkg::lzn_decompress(prosperopkg::lzn_compress(randomish, 4)) == randomish);

    auto truncated = compressed;
    truncated.pop_back();
    expect_exception([&] { (void)prosperopkg::lzn_decompress(truncated); });

    auto trailing = compressed;
    trailing.push_back(std::byte{0});
    expect_exception([&] { (void)prosperopkg::lzn_decompress(trailing); });

    auto bad_magic = compressed;
    bad_magic[0] = std::byte{'X'};
    expect_exception([&] { (void)prosperopkg::lzn_decompress(bad_magic); });
}

void test_lzn_block_roundtrip()
{
    std::vector<std::byte> input(0x91000);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::byte>((i / 97u + i / 4096u * 13u) & 0xFFu);
    }

    prosperopkg::LznBlockOptions options;
    options.block_size = 0x10000;
    options.level = 2;

    const auto archive = prosperopkg::lzn_block_compress(input, options);
    CHECK(prosperopkg::is_lzn_block_archive(archive));
    const auto info = prosperopkg::read_lzn_block_info(archive);
    CHECK(info.version == 1u);
    CHECK(info.block_size == options.block_size);
    CHECK(info.block_count == 10u);
    CHECK(info.original_size == input.size());
    CHECK(archive.size() < input.size());

    const auto entries = prosperopkg::read_lzn_block_entries(archive);
    CHECK(entries.size() == info.block_count);
    CHECK(std::any_of(entries.begin(), entries.end(), [](const auto& entry) {
        return !entry.stored_raw();
    }));
    CHECK(prosperopkg::lzn_block_decompress(archive) == input);
    std::vector<std::byte> decoded(input.size());
    CHECK(prosperopkg::lzn_block_decompress_to(archive, decoded) == input.size());
    CHECK(decoded == input);

    const std::size_t range_offset = 0x12345;
    const std::size_t range_size = 0x23456;
    const auto range = prosperopkg::lzn_block_decompress_range(archive, range_offset, range_size);
    CHECK(range.size() == range_size);
    CHECK(std::equal(
        range.begin(),
        range.end(),
        input.begin() + static_cast<std::ptrdiff_t>(range_offset)));

    auto corrupted = archive;
    corrupted[static_cast<std::size_t>(entries.front().offset)] =
        static_cast<std::byte>(static_cast<unsigned char>(corrupted[static_cast<std::size_t>(entries.front().offset)]) ^ 0x55u);
    expect_exception([&] { (void)prosperopkg::lzn_block_decompress(corrupted); });
}

void test_self_parse()
{
    constexpr std::size_t header_size = 0xC0;
    std::vector<std::byte> image(header_size);
    write_le32(image, 0x00, prosperopkg::SelfLayout::magic);
    write_le32(image, 0x08, 0x00000101);
    write_le16(image, 0x0C, static_cast<std::uint16_t>(header_size));
    write_le16(image, 0x0E, 0x0010);
    write_le64(image, 0x10, image.size());
    write_le16(image, 0x18, 1);

    write_le64(image, 0x20, (5ull << 20u) | 0x280Fu);
    write_le64(image, 0x28, 0xA0);
    write_le64(image, 0x30, 0x20);
    write_le64(image, 0x38, 0x40);

    const std::size_t elf_start = 0x40;
    image[elf_start] = std::byte{0x7F};
    image[elf_start + 1] = std::byte{'E'};
    image[elf_start + 2] = std::byte{'L'};
    image[elf_start + 3] = std::byte{'F'};
    image[elf_start + 4] = std::byte{2};
    write_le16(image, elf_start + 0x38, 0);

    const std::size_t ext_start = 0x80;
    write_le64(image, ext_start, 0x3100000000000001ull);
    write_le64(image, ext_start + 0x08, 1);
    write_le64(image, ext_start + 0x10, 0x0100);
    write_le64(image, ext_start + 0x18, 0x0900);
    image[ext_start + 0x20] = std::byte{0xAA};

    CHECK(prosperopkg::is_self(image));
    CHECK(prosperopkg::validate_self(image));
    const auto parsed = prosperopkg::parse_self(image);
    CHECK(parsed.program_type == 0x00000101u);
    CHECK(parsed.header_size == static_cast<int>(header_size));
    CHECK(parsed.meta_size == 0x10);
    CHECK(parsed.file_size == image.size());
    CHECK(parsed.segments.size() == 1u);
    CHECK(parsed.segments[0].id() == 5);
    CHECK(parsed.segments[0].signed_segment());
    CHECK(parsed.elf.size() == prosperopkg::SelfLayout::elf_header_size);
    CHECK(parsed.ext_info.has_value());
    CHECK(parsed.ext_info->authority_id == 0x3100000000000001ull);
    CHECK(parsed.ext_info->digest[0] == std::byte{0xAA});
}

void test_make_fself()
{
    const auto elf = make_test_elf();
    prosperopkg::FselfOptions options;
    options.app_version = 0x0100;
    options.firmware_version = 0x0900;

    const auto self = prosperopkg::make_fself(elf, options);
    CHECK(prosperopkg::is_self(self));

    const auto parsed = prosperopkg::parse_self(self);
    CHECK(parsed.segments.size() == 2u);
    CHECK(parsed.segments[0].id() == 1);
    CHECK(parsed.segments[0].file_size == 0x20u);
    CHECK(parsed.segments[1].id() == 0);
    CHECK(parsed.segments[1].file_size == 0x10u);
    CHECK(parsed.ext_info.has_value());
    CHECK(parsed.ext_info->authority_id == 0x3100000000000001ull);
    CHECK(parsed.ext_info->app_version == 0x0100u);
    CHECK(parsed.ext_info->firmware_version == 0x0900u);
    CHECK(parsed.ext_info->digest == prosperopkg::sha256(elf));

    const auto payload_off = static_cast<std::size_t>(parsed.segments[1].file_offset);
    CHECK(payload_off + 0x10 <= self.size());
    CHECK(std::equal(
        elf.begin() + 0x100,
        elf.begin() + 0x110,
        self.begin() + static_cast<std::ptrdiff_t>(payload_off)));
}

void test_gp5_normal_serializer()
{
    auto project = prosperopkg::create_gp5_project(
        prosperopkg::Gp5VolumeType::prospero_app,
        "11111111111111111111111111111111");
    project.root_dir.source_path = "/tmp/source";
    project.root_dir.dir_exclude = prosperopkg::gp5_default_dir_exclude;
    project.root_dir.file_exclude = prosperopkg::gp5_default_file_exclude;

    const std::string xml = prosperopkg::gp5_to_xml(project);
    CHECK(xml.find("<?xml version=\"1.0\" encoding=\"utf-8\"?>") == 0);
    CHECK(xml.find("<psproject fmt=\"gp5\" version=\"1000\">") != std::string::npos);
    CHECK(xml.find("<volume_type>prospero_app</volume_type>") != std::string::npos);
    CHECK(xml.find("<package passcode=\"11111111111111111111111111111111\" />") != std::string::npos);
    CHECK(xml.find("<chunk_info chunk_count=\"1\" scenario_count=\"1\">") != std::string::npos);
    CHECK(xml.find("<global_exclude></global_exclude>") != std::string::npos);
    CHECK(xml.find("<rootdir dir_exclude=\"about\"") != std::string::npos);
}

void test_gp5_flat_creator()
{
    const std::filesystem::path root = unique_temp_path("prosperopkg-gp5-flat-test");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sce_sys");
    std::filesystem::create_directories(root / "data");
    write_text_file(root / "sce_sys" / "param.json", "{}");
    write_text_file(root / "data" / "file.bin", "bin");

    const auto project = prosperopkg::gp5_from_folder_explicit(root);
    std::filesystem::remove_all(root);

    CHECK(project.layout() == prosperopkg::Gp5Layout::flat);
    CHECK(project.files.size() == 2u);
    const std::string xml = prosperopkg::gp5_to_xml(project);
    CHECK(xml.find("<files>") != std::string::npos);
    CHECK(xml.find("dst_path=\"data\\file.bin\"") != std::string::npos);
    CHECK(xml.find("dst_path=\"sce_sys\\param.json\"") != std::string::npos);
    CHECK(xml.find("<rootdir") == std::string::npos);
}

void test_gp5_xml_escape()
{
    auto project = prosperopkg::create_gp5_project(prosperopkg::Gp5VolumeType::prospero_ac);
    project.volume.package.content_id = "UP9000-PPSA00000_00-A&B<QUOTE>0000";
    project.files.push_back(prosperopkg::Gp5File{"sce_sys\\param.json", "C:\\A&B\\param.json"});

    const std::string xml = prosperopkg::gp5_to_xml(project);
    CHECK(xml.find("content_id=\"UP9000-PPSA00000_00-A&amp;B&lt;QUOTE&gt;0000\"") != std::string::npos);
    CHECK(xml.find("src_path=\"C:\\A&amp;B\\param.json\"") != std::string::npos);
}

void test_tool_selftest()
{
#ifdef PROSPEROPKG_INSPECT_PATH
    CHECK(run_program(PROSPEROPKG_INSPECT_PATH, {"--self-test"}) == 0);
#endif
}

void test_tool_usage_errors()
{
#ifdef PROSPEROPKG_INSPECT_PATH
    expect_tool_usage_failure(PROSPEROPKG_INSPECT_PATH);
#endif
#ifdef PROSPEROPKG_FSELF_PATH
    expect_tool_usage_failure(PROSPEROPKG_FSELF_PATH);
#endif
#ifdef PROSPEROPKG_GP5_PATH
    expect_tool_usage_failure(PROSPEROPKG_GP5_PATH);
#endif
#ifdef PROSPEROPKG_KEYS_PATH
    expect_tool_usage_failure(PROSPEROPKG_KEYS_PATH);
#endif
#ifdef PROSPEROPKG_LZN_PATH
    expect_tool_usage_failure(PROSPEROPKG_LZN_PATH);
#endif
}

void test_tool_inspects_ucp()
{
#ifdef PROSPEROPKG_INSPECT_PATH
    const std::filesystem::path path = unique_temp_path("prosperopkg-tool-test", ".ucp");
    const auto archive = prosperopkg::build_ucp(std::vector<prosperopkg::UcpEntry>{
        {"tool.txt", bytes("hello")},
    });

    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        CHECK(file.good());
        file.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
        CHECK(file.good());
    }

    const int rc = run_program(PROSPEROPKG_INSPECT_PATH, {path.string()});
    std::filesystem::remove(path);
    CHECK(rc == 0);
#endif
}

void test_tool_reports_pkg_digests()
{
#ifdef PROSPEROPKG_INSPECT_PATH
    const std::filesystem::path pkg_path = unique_temp_path("prosperopkg-tool-digests-test", ".pkg");
    const std::filesystem::path output = unique_temp_path("prosperopkg-tool-digests-test", ".txt");

    const auto image = prosperopkg::write_cnt(minimal_options());
    write_file(pkg_path, image);

    const int rc = run_program(PROSPEROPKG_INSPECT_PATH, {pkg_path.string()}, &output);
    CHECK(rc == 0);

    const auto out_bytes = read_file(output);
    const std::string out(reinterpret_cast<const char*>(out_bytes.data()), out_bytes.size());
    std::filesystem::remove(pkg_path);
    std::filesystem::remove(output);

    CHECK(out.find("Type: Meta") != std::string::npos);
    CHECK(out.find("Digests:") != std::string::npos);
    CHECK(out.find("Package: ") != std::string::npos);
    CHECK(out.find("Body: ") != std::string::npos);
    CHECK(out.find("Entry digest table size: 0x60") != std::string::npos);
    CHECK(out.find("[0] 0x1000") != std::string::npos);
    CHECK(out.find("[1] 0x1200") != std::string::npos);
    CHECK(out.find("[2] 0x0200") != std::string::npos);
#endif
}

void test_tool_builds_fself()
{
#ifdef PROSPEROPKG_FSELF_PATH
    const std::filesystem::path elf_path = unique_temp_path("prosperopkg-tool-test", ".elf");
    const std::filesystem::path self_path = unique_temp_path("prosperopkg-tool-test", ".self");

    const auto elf = make_test_elf();
    write_file(elf_path, elf);

    const int rc = run_program(PROSPEROPKG_FSELF_PATH,
                               {elf_path.string(), self_path.string(), "0x100", "0x900"});
    CHECK(rc == 0);

    const auto self = read_file(self_path);
    std::filesystem::remove(elf_path);
    std::filesystem::remove(self_path);

    const auto parsed = prosperopkg::parse_self(self);
    CHECK(parsed.ext_info.has_value());
    CHECK(parsed.ext_info->app_version == 0x100u);
    CHECK(parsed.ext_info->firmware_version == 0x900u);
#endif
}

void test_tool_builds_gp5()
{
#ifdef PROSPEROPKG_GP5_PATH
    const std::filesystem::path root = unique_temp_path("prosperopkg-tool-gp5-test");
    const std::filesystem::path output = unique_temp_path("prosperopkg-tool-test", ".gp5");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sce_sys");
    write_text_file(root / "sce_sys" / "param.json", "{}");

    const int rc = run_program(PROSPEROPKG_GP5_PATH,
                               {root.string(), output.string(), "--flat", "--type", "app"});
    CHECK(rc == 0);

    const auto xml_bytes = read_file(output);
    const std::string xml(reinterpret_cast<const char*>(xml_bytes.data()), xml_bytes.size());
    std::filesystem::remove_all(root);
    std::filesystem::remove(output);

    CHECK(xml.find("<files>") != std::string::npos);
    CHECK(xml.find("dst_path=\"sce_sys\\param.json\"") != std::string::npos);
#endif
}

void test_tool_derives_keys()
{
#ifdef PROSPEROPKG_KEYS_PATH
    const std::filesystem::path output = unique_temp_path("prosperopkg-tool-keys-test", ".txt");

    const int rc = run_program(PROSPEROPKG_KEYS_PATH,
                               {"UP9000-PPSA00000_00-PROSPERO00000000",
                                "00000000000000000000000000000000",
                                "000102030405060708090a0b0c0d0e0f"},
                               &output);
    CHECK(rc == 0);

    const auto out_bytes = read_file(output);
    const std::string out(reinterpret_cast<const char*>(out_bytes.data()), out_bytes.size());
    std::filesystem::remove(output);

    CHECK(out.find("EKPFS: d2a92aa1153b6c73ee56ec279cb5bd4821d1215e97bea5995cc93443bdc2aae9") !=
          std::string::npos);
    CHECK(out.find("Tweak key: b3f7f686e56b0f01f091418e0f684b8e") != std::string::npos);
    CHECK(out.find("Data key: 4bca2b69b4e1098e22de2c28a754ff34") != std::string::npos);
    CHECK(out.find("Sign key: c527a73dfce9e17afa13b2ef57ce1411e4cda5b71969416a6097c6b4311a16c6") !=
          std::string::npos);
#endif
}

void test_tool_lzn_roundtrip()
{
#ifdef PROSPEROPKG_LZN_PATH
    const std::filesystem::path input_path = unique_temp_path("prosperopkg-tool-lzn-test", ".bin");
    const std::filesystem::path compressed_path = unique_temp_path("prosperopkg-tool-lzn-test", ".lzn");
    const std::filesystem::path output_path = unique_temp_path("prosperopkg-tool-lzn-test", ".out");
    const std::filesystem::path bench_output = unique_temp_path("prosperopkg-tool-lzn-test", ".txt");

    std::vector<std::byte> input(0x30000);
    for (std::size_t i = 0; i < input.size(); ++i) {
        input[i] = static_cast<std::byte>((i / 32u + i / 4096u) & 0xFFu);
    }
    write_file(input_path, input);

    CHECK(run_program(PROSPEROPKG_LZN_PATH,
                      {"compress", input_path.string(), compressed_path.string(), "2"}) == 0);
    CHECK(run_program(PROSPEROPKG_LZN_PATH,
                      {"decompress", compressed_path.string(), output_path.string()}) == 0);
    CHECK(read_file(output_path) == input);

    CHECK(run_program(PROSPEROPKG_LZN_PATH,
                      {"bench", input_path.string(), "2", "2"},
                      &bench_output) == 0);
    const auto bench_bytes = read_file(bench_output);
    const std::string bench_text(reinterpret_cast<const char*>(bench_bytes.data()), bench_bytes.size());
    CHECK(bench_text.find("LZN1 benchmark") != std::string::npos);
    CHECK(bench_text.find("decompress:") != std::string::npos);

    const std::filesystem::path block_path = unique_temp_path("prosperopkg-tool-lznb-test", ".lznb");
    const std::filesystem::path block_output_path = unique_temp_path("prosperopkg-tool-lznb-test", ".out");
    const std::filesystem::path block_info_output = unique_temp_path("prosperopkg-tool-lznb-test", ".txt");
    const std::filesystem::path block_bench_output = unique_temp_path("prosperopkg-tool-lznb-test", ".txt");

    CHECK(run_program(PROSPEROPKG_LZN_PATH,
                      {"block-compress", input_path.string(), block_path.string(), "2", "65536"}) == 0);
    CHECK(run_program(PROSPEROPKG_LZN_PATH,
                      {"block-decompress", block_path.string(), block_output_path.string()}) == 0);
    CHECK(read_file(block_output_path) == input);
    CHECK(run_program(PROSPEROPKG_LZN_PATH,
                      {"block-info", block_path.string()},
                      &block_info_output) == 0);
    const auto block_info_bytes = read_file(block_info_output);
    const std::string block_info_text(reinterpret_cast<const char*>(block_info_bytes.data()), block_info_bytes.size());
    CHECK(block_info_text.find("LZNB archive") != std::string::npos);
    CHECK(block_info_text.find("blocks:") != std::string::npos);
    CHECK(run_program(PROSPEROPKG_LZN_PATH,
                      {"block-bench", input_path.string(), "2", "2", "65536"},
                      &block_bench_output) == 0);
    const auto block_bench_bytes = read_file(block_bench_output);
    const std::string block_bench_text(reinterpret_cast<const char*>(block_bench_bytes.data()), block_bench_bytes.size());
    CHECK(block_bench_text.find("LZNB benchmark") != std::string::npos);

    std::filesystem::remove(input_path);
    std::filesystem::remove(compressed_path);
    std::filesystem::remove(output_path);
    std::filesystem::remove(bench_output);
    std::filesystem::remove(block_path);
    std::filesystem::remove(block_output_path);
    std::filesystem::remove(block_info_output);
    std::filesystem::remove(block_bench_output);
#endif
}

using TestFn = void (*)();

} // namespace

int main()
{
    const std::array<std::pair<std::string_view, TestFn>, 34> tests{{
        {"content-id", test_content_id},
        {"crc32c", test_crc32c},
        {"sha256", test_sha256},
        {"sha3-256", test_sha3_256},
        {"hmac-sha256", test_hmac_sha256},
        {"aes128-block", test_aes128_block},
        {"xts-transform-unit", test_xts_transform_unit},
        {"pfs-keys", test_pfs_keys},
        {"pfs-image-transforms", test_pfs_image_transforms},
        {"pfs-signature", test_pfs_signature},
        {"image-digests", test_image_digests},
        {"cnt-writer-reader-roundtrip", test_cnt_writer_reader_roundtrip},
        {"fih-embedded-cnt-reader", test_fih_embedded_cnt_reader},
        {"native-inner-image-builder", test_native_inner_image_builder},
        {"native-package-builder", test_native_package_builder},
        {"ucp-build-read-verify-repair", test_ucp_build_read_verify_repair},
        {"pfsc-raw-roundtrip", test_pfsc_raw_roundtrip},
        {"pfsc-zlib-roundtrip", test_pfsc_zlib_roundtrip},
        {"pfsc-pfs-v3-stored-roundtrip", test_pfsc_pfs_v3_stored_roundtrip},
        {"lzn-roundtrip", test_lzn_roundtrip},
        {"lzn-block-roundtrip", test_lzn_block_roundtrip},
        {"self-parse", test_self_parse},
        {"make-fself", test_make_fself},
        {"gp5-normal-serializer", test_gp5_normal_serializer},
        {"gp5-flat-creator", test_gp5_flat_creator},
        {"gp5-xml-escape", test_gp5_xml_escape},
        {"tool-selftest", test_tool_selftest},
        {"tool-usage-errors", test_tool_usage_errors},
        {"tool-inspects-ucp", test_tool_inspects_ucp},
        {"tool-reports-pkg-digests", test_tool_reports_pkg_digests},
        {"tool-builds-fself", test_tool_builds_fself},
        {"tool-builds-gp5", test_tool_builds_gp5},
        {"tool-derives-keys", test_tool_derives_keys},
        {"tool-lzn-roundtrip", test_tool_lzn_roundtrip},
    }};

    int failed = 0;
    for (const auto& [name, fn] : tests) {
        try {
            fn();
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& ex) {
            ++failed;
            std::cerr << "[FAIL] " << name << ": " << ex.what() << '\n';
        }
    }

    return failed == 0 ? 0 : 1;
}
