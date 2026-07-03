// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/prosperopkg.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using prosperopkg::EntryId;

[[nodiscard]] std::vector<std::byte> bytes(std::string_view value)
{
    std::vector<std::byte> out;
    out.reserve(value.size());
    for (char ch : value) {
        out.push_back(static_cast<std::byte>(ch));
    }
    return out;
}

[[nodiscard]] std::vector<std::byte> read_all(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open file for reading: " + path.string());
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("Could not determine file size: " + path.string());
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            throw std::runtime_error("Could not read file: " + path.string());
        }
    }
    return data;
}

void print_hex(std::span<const std::byte> data)
{
    for (std::byte b : data) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(static_cast<unsigned char>(b));
    }
    std::cout << std::dec << std::setfill(' ');
}

template <std::size_t N>
void print_digest_line(std::string_view label, const std::array<std::byte, N>& digest)
{
    std::cout << "  " << label << ": ";
    print_hex(digest);
    std::cout << '\n';
}

[[nodiscard]] std::optional<std::span<const std::byte>> checked_span(
    const std::vector<std::byte>& data,
    std::uint64_t offset,
    std::uint64_t size)
{
    if (offset > data.size() || size > data.size() - offset) {
        return std::nullopt;
    }
    return std::span<const std::byte>(data).subspan(
        static_cast<std::size_t>(offset),
        static_cast<std::size_t>(size));
}

void print_digest_error(std::string_view label, const std::exception& ex)
{
    std::cout << "  " << label << ": n/a (" << ex.what() << ")\n";
}

[[nodiscard]] int self_test()
{
    prosperopkg::PkgWriterOptions options;
    options.content_id = prosperopkg::compose_content_id("UP9000", "PPSA00000", "INSPECT");
    options.entries.push_back(prosperopkg::PkgWriterEntry{
        static_cast<std::uint32_t>(EntryId::param_json),
        "sce_sys/param.json",
        bytes("{\"titleName\":\"Inspect\"}"),
        0,
        0});

    const auto image = prosperopkg::write_cnt(options);
    std::string backing(reinterpret_cast<const char*>(image.data()), image.size());
    std::istringstream stream(backing, std::ios::binary);
    const auto parsed = prosperopkg::read_pkg(stream);

    if (parsed.type != prosperopkg::PackageType::meta ||
        !parsed.header ||
        parsed.header->content_id != options.content_id ||
        parsed.entries.size() != 2 ||
        parsed.entries[0].name != "sce_sys/param.json") {
        std::cerr << "prosperopkg-inspect self-test failed\n";
        return 1;
    }

    std::cout << "prosperopkg-inspect self-test passed\n";
    return 0;
}

void print_ucp(std::span<const std::byte> data)
{
    std::string error;
    const bool valid = prosperopkg::validate_ucp(data, &error);
    std::cout << "Type: UCP\n";
    std::cout << "Valid: " << (valid ? "yes" : "no") << '\n';
    if (!valid) {
        std::cout << "Error: " << error << '\n';
        return;
    }

    std::cout << "Digest: " << (prosperopkg::verify_ucp_digest(data) ? "ok" : "mismatch") << '\n';
    const auto entries = prosperopkg::read_ucp(data);
    std::cout << "Entries: " << entries.size() << '\n';
    for (std::size_t i = 0; i < entries.size(); ++i) {
        std::cout << "  [" << i << "] " << entries[i].name
                  << " size=0x" << std::hex << entries[i].data.size()
                  << std::dec << '\n';
    }
}

void print_self(std::span<const std::byte> data)
{
    std::string error;
    const bool valid = prosperopkg::validate_self(data, &error);
    std::cout << "Type: SELF\n";
    std::cout << "Valid: " << (valid ? "yes" : "no") << '\n';
    if (!valid) {
        std::cout << "Error: " << error << '\n';
        return;
    }

    const auto self = prosperopkg::parse_self(data);
    std::cout << "Program type: 0x" << std::hex << self.program_type << std::dec << '\n';
    std::cout << "Header size:  0x" << std::hex << self.header_size << std::dec << '\n';
    std::cout << "Meta size:    0x" << std::hex << self.meta_size << std::dec << '\n';
    std::cout << "File size:    0x" << std::hex << self.file_size << std::dec << '\n';
    std::cout << "Segments:     " << self.segments.size() << '\n';
    for (std::size_t i = 0; i < self.segments.size(); ++i) {
        const auto& segment = self.segments[i];
        std::cout << "  [" << i << "] id=" << segment.id()
                  << " off=0x" << std::hex << segment.file_offset
                  << " size=0x" << segment.file_size
                  << " mem=0x" << segment.mem_size
                  << std::dec;
        if (segment.encrypted()) {
            std::cout << " encrypted";
        }
        if (segment.signed_segment()) {
            std::cout << " signed";
        }
        if (segment.compressed()) {
            std::cout << " compressed";
        }
        std::cout << '\n';
    }

    if (self.ext_info) {
        std::cout << "Authority ID: 0x" << std::hex << self.ext_info->authority_id << std::dec << '\n';
    }
}

void print_pkg_digests(const prosperopkg::Pkg& pkg, const std::vector<std::byte>& data)
{
    if (!pkg.header) {
        return;
    }

    const std::uint64_t cnt_base = pkg.fih ? pkg.fih->embedded_cnt_offset : 0;
    std::cout << "Digests:\n";
    if (cnt_base > data.size()) {
        std::cout << "  CNT: n/a (embedded CNT is outside the file)\n";
        return;
    }
    const auto cnt = checked_span(data, cnt_base, data.size() - static_cast<std::size_t>(cnt_base));
    if (!cnt) {
        std::cout << "  CNT: n/a (embedded CNT is outside the file)\n";
        return;
    }

    try {
        print_digest_line("Package", prosperopkg::compute_package_digest(*cnt));
    } catch (const std::exception& ex) {
        print_digest_error("Package", ex);
    }

    try {
        print_digest_line("CNT rollup", prosperopkg::compute_cnt_header_rollup_digest(*cnt));
    } catch (const std::exception& ex) {
        print_digest_error("CNT rollup", ex);
    }

    const auto& header = *pkg.header;
    if (const auto body = checked_span(data, cnt_base + header.body_offset, header.body_size)) {
        print_digest_line("Body", prosperopkg::compute_body_digest(*body));
    } else {
        std::cout << "  Body: n/a (body region is outside the file)\n";
    }

    std::vector<prosperopkg::CntDigestPayload> payloads;
    payloads.reserve(pkg.entries.size());
    for (const auto& entry : pkg.entries) {
        if (const auto payload = checked_span(data, cnt_base + entry.data_offset, entry.data_size)) {
            payloads.push_back(prosperopkg::CntDigestPayload{entry.raw_id, *payload});
        } else {
            payloads.push_back(prosperopkg::CntDigestPayload{entry.raw_id, {}});
        }
    }
    if (!payloads.empty()) {
        try {
            const auto table = prosperopkg::build_entry_digest_table(payloads);
            std::cout << "  Entry digest table size: 0x" << std::hex << table.size() << std::dec << '\n';
            for (std::size_t i = 0; i < pkg.entries.size(); ++i) {
                std::cout << "    [" << i << "] 0x"
                          << std::hex << std::setw(4) << std::setfill('0') << pkg.entries[i].raw_id
                          << std::dec << std::setfill(' ') << " ";
                print_hex(std::span<const std::byte>(table.data(), table.size()).subspan(
                    i * prosperopkg::image_digest_size,
                    prosperopkg::image_digest_size));
                std::cout << '\n';
            }
        } catch (const std::exception& ex) {
            print_digest_error("Entry digest table", ex);
        }
    }

    if (pkg.fih) {
        if (const auto fixed_info = checked_span(data, 0, prosperopkg::image_digest_block_size)) {
            try {
                print_digest_line("Fixed info", prosperopkg::compute_fixed_info_digest(*fixed_info));
            } catch (const std::exception& ex) {
                print_digest_error("Fixed info", ex);
            }
        }

        if (pkg.fih->pfs_image_size > 0) {
            if (const auto image = checked_span(data, pkg.fih->pfs_image_offset, pkg.fih->pfs_image_size)) {
                const auto sblock = prosperopkg::compute_sblock_digest_from_image(*image);
                if (sblock) {
                    std::cout << "  Sblock offset: 0x" << std::hex << sblock->offset << std::dec << '\n';
                    print_digest_line("Sblock", sblock->digest);
                } else {
                    std::cout << "  Sblock: n/a (plaintext superblock not found)\n";
                }
            } else {
                std::cout << "  Sblock: n/a (PFS image region is outside the file)\n";
            }
        }
    }
}

void print_pkg(const prosperopkg::Pkg& pkg, const std::vector<std::byte>& data)
{
    std::cout << "Type: " << prosperopkg::to_string(pkg.type) << '\n';

    if (pkg.fih) {
        std::cout << "FIH signed byte: 0x"
                  << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(pkg.fih->signed_byte)
                  << std::dec << std::setfill(' ') << '\n';
        std::cout << "FIH PFS image offset: 0x" << std::hex << pkg.fih->pfs_image_offset << std::dec << '\n';
        std::cout << "FIH PFS image size:   0x" << std::hex << pkg.fih->pfs_image_size << std::dec << '\n';
        std::cout << "FIH embedded CNT:     0x" << std::hex << pkg.fih->embedded_cnt_offset << std::dec << '\n';
    }

    if (!pkg.header) {
        return;
    }

    const auto& header = *pkg.header;
    std::cout << "Content ID: " << header.content_id << '\n';
    std::cout << "Entries:    " << header.entry_count << '\n';
    std::cout << "Body:       0x" << std::hex << header.body_offset
              << "..0x" << (header.body_offset + header.body_size)
              << std::dec << '\n';

    for (std::size_t i = 0; i < pkg.entries.size(); ++i) {
        const auto& entry = pkg.entries[i];
        std::cout << "  [" << i << "] 0x"
                  << std::hex << std::setw(4) << std::setfill('0') << entry.raw_id
                  << std::dec << std::setfill(' ')
                  << " " << prosperopkg::to_string(entry.id)
                  << " off=0x" << std::hex << entry.data_offset
                  << " size=0x" << entry.data_size
                  << std::dec;

        if (!entry.name.empty()) {
            std::cout << " name=" << entry.name;
        }
        if (entry.encrypted()) {
            std::cout << " encrypted key=" << entry.key_index();
        }
        std::cout << '\n';
    }

    print_pkg_digests(pkg, data);
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--self-test") {
            return self_test();
        }

        if (argc != 2) {
            std::cerr << "usage: prosperopkg-inspect <file>\n";
            std::cerr << "       prosperopkg-inspect --self-test\n";
            return 2;
        }

        const std::filesystem::path path(argv[1]);
        const auto pkg_type = prosperopkg::detect_type(path);
        if (pkg_type) {
            const auto data = read_all(path);
            print_pkg(prosperopkg::read_pkg(path), data);
            return 0;
        }

        const auto data = read_all(path);
        if (prosperopkg::is_ucp(data)) {
            print_ucp(data);
            return 0;
        }
        if (prosperopkg::is_self(data)) {
            print_self(data);
            return 0;
        }

        throw std::runtime_error("Unsupported file type.");
    } catch (const std::exception& ex) {
        std::cerr << "prosperopkg-inspect: " << ex.what() << '\n';
        return 1;
    }
}
