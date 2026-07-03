// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/self.hpp>

#include <prosperopkg/hash.hpp>

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace prosperopkg {
namespace {

[[nodiscard]] std::uint16_t read_le16(const std::byte* data) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(data[0]) |
        (static_cast<std::uint16_t>(data[1]) << 8u));
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

void write_le16(std::byte* data, std::uint16_t value) noexcept
{
    data[0] = static_cast<std::byte>(value & 0xFFu);
    data[1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
}

void write_le32(std::byte* data, std::uint32_t value) noexcept
{
    data[0] = static_cast<std::byte>(value & 0xFFu);
    data[1] = static_cast<std::byte>((value >> 8u) & 0xFFu);
    data[2] = static_cast<std::byte>((value >> 16u) & 0xFFu);
    data[3] = static_cast<std::byte>((value >> 24u) & 0xFFu);
}

void write_le64(std::byte* data, std::uint64_t value) noexcept
{
    write_le32(data, static_cast<std::uint32_t>(value & 0xFFFFFFFFu));
    write_le32(data + 4, static_cast<std::uint32_t>(value >> 32u));
}

[[nodiscard]] int align_up(int value, int alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

struct SelectedSegment {
    int phdr_index = 0;
    int file_offset = 0;
    int file_size = 0;
};

constexpr int control_region_size = 0x30;
constexpr int meta_footer_base = 0x110;
constexpr int digest_segment_size = 0x20;
constexpr int footer_marker_offset = 0x3F0;
constexpr std::uint32_t default_program_type = 0x00000101u;
constexpr int ex_info_byte_offset = 0x3F00;

constexpr std::uint64_t paid_exec = 0x3100000000000001ull;
constexpr std::uint64_t paid_dynamic = 0x3100000000000002ull;
constexpr std::uint64_t paid_exec_a = 0x3100000000001101ull;
constexpr std::uint64_t paid_dynamic_a = 0x3100000000001102ull;
constexpr std::uint64_t paid_exec_b = 0x3100000000001001ull;
constexpr std::uint64_t paid_dynamic_b = 0x3100000000001002ull;

constexpr std::uint32_t pt_load = 0x00000001u;
constexpr std::uint32_t pt_module_data = 0x61000000u;
constexpr std::uint32_t pt_relro = 0x61000010u;
constexpr std::uint32_t pt_comment = 0x6FFFFF00u;

[[nodiscard]] bool fits_int(std::size_t value) noexcept
{
    return value <= static_cast<std::size_t>(std::numeric_limits<int>::max());
}

[[nodiscard]] std::vector<SelectedSegment> select_segments(
    std::span<const std::byte> elf,
    int phoff,
    int phnum)
{
    std::vector<SelectedSegment> result;
    for (int i = 0; i < phnum; ++i) {
        const int p = phoff + i * static_cast<int>(SelfLayout::elf_phdr_size);
        const std::uint32_t p_type = read_le32(elf.data() + p);
        const std::uint64_t off = read_le64(elf.data() + p + 0x08);
        const std::uint64_t file_size = read_le64(elf.data() + p + 0x20);
        if (file_size == 0 || off > elf.size() || file_size > elf.size() - off) {
            continue;
        }
        if (!fits_int(static_cast<std::size_t>(off)) ||
            !fits_int(static_cast<std::size_t>(file_size))) {
            continue;
        }
        if (p_type == pt_load || p_type == pt_module_data ||
            p_type == pt_relro || p_type == pt_comment) {
            result.push_back(SelectedSegment{
                i,
                static_cast<int>(off),
                static_cast<int>(file_size)});
        }
    }
    return result;
}

[[nodiscard]] std::uint64_t derive_authority_id(std::span<const std::byte> elf, std::uint16_t e_type)
{
    const bool exec = e_type == 0x02u || e_type == 0xFE00u || e_type == 0xFE10u;
    const std::byte ex = ex_info_byte_offset < static_cast<int>(elf.size())
        ? elf[ex_info_byte_offset]
        : std::byte{0};

    if (ex == std::byte{0x40}) {
        return exec ? paid_exec_a : paid_dynamic_a;
    }
    if (ex == std::byte{0x80}) {
        return exec ? paid_exec_b : paid_dynamic_b;
    }
    return exec ? paid_exec : paid_dynamic;
}

void write_segment(
    std::span<std::byte> out,
    int entry,
    std::uint64_t flags,
    std::uint64_t offset,
    std::uint64_t file_size,
    std::uint64_t mem_size)
{
    write_le64(out.data() + entry, flags);
    write_le64(out.data() + entry + 0x08, offset);
    write_le64(out.data() + entry + 0x10, file_size);
    write_le64(out.data() + entry + 0x18, mem_size);
}

} // namespace

int SelfSegment::id() const noexcept
{
    return static_cast<int>((flags >> 20u) & 0xFFFFu);
}

bool SelfSegment::ordered() const noexcept
{
    return (flags & 0x1u) != 0u;
}

bool SelfSegment::encrypted() const noexcept
{
    return (flags & 0x2u) != 0u;
}

bool SelfSegment::signed_segment() const noexcept
{
    return (flags & 0x4u) != 0u;
}

bool SelfSegment::compressed() const noexcept
{
    return (flags & 0x8u) != 0u;
}

bool SelfSegment::blocked() const noexcept
{
    return (flags & 0x800u) != 0u;
}

bool is_self(std::span<const std::byte> data) noexcept
{
    return data.size() >= SelfLayout::sce_header_size &&
           read_le32(data.data()) == SelfLayout::magic;
}

bool is_elf(std::span<const std::byte> data) noexcept
{
    return data.size() >= SelfLayout::elf_header_size &&
           data[0] == std::byte{0x7F} &&
           data[1] == std::byte{'E'} &&
           data[2] == std::byte{'L'} &&
           data[3] == std::byte{'F'};
}

bool validate_self(std::span<const std::byte> data, std::string* error)
{
    auto fail = [error](std::string message) {
        if (error != nullptr) {
            *error = std::move(message);
        }
        return false;
    };

    if (data.size() < SelfLayout::sce_header_size) {
        return fail("Buffer is smaller than an SCE header.");
    }
    if (read_le32(data.data()) != SelfLayout::magic) {
        return fail("Bad SCE magic.");
    }

    const int header_size = read_le16(data.data() + 0x0C);
    const int segment_count = read_le16(data.data() + 0x18);
    const std::uint64_t table_end =
        SelfLayout::sce_header_size +
        static_cast<std::uint64_t>(segment_count) * SelfLayout::segment_entry_size;

    if (table_end > data.size()) {
        return fail("Segment table overruns the buffer.");
    }
    if (header_size > static_cast<int>(data.size())) {
        return fail("Header size exceeds the buffer.");
    }

    if (error != nullptr) {
        error->clear();
    }
    return true;
}

SelfImage parse_self(std::span<const std::byte> data)
{
    std::string error;
    if (!validate_self(data, &error)) {
        throw std::runtime_error(error);
    }

    SelfImage image;
    image.program_type = read_le32(data.data() + 0x08);
    image.header_size = read_le16(data.data() + 0x0C);
    image.meta_size = read_le16(data.data() + 0x0E);
    image.file_size = read_le64(data.data() + 0x10);

    const int segment_count = read_le16(data.data() + 0x18);
    image.segments.reserve(segment_count);
    for (int i = 0; i < segment_count; ++i) {
        const std::size_t entry =
            SelfLayout::sce_header_size + static_cast<std::size_t>(i) * SelfLayout::segment_entry_size;
        image.segments.push_back(SelfSegment{
            read_le64(data.data() + entry),
            read_le64(data.data() + entry + 0x08),
            read_le64(data.data() + entry + 0x10),
            read_le64(data.data() + entry + 0x18)});
    }

    const int elf_start =
        static_cast<int>(SelfLayout::sce_header_size) +
        segment_count * static_cast<int>(SelfLayout::segment_entry_size);
    if (elf_start >= 0 && static_cast<std::size_t>(elf_start) < data.size() &&
        is_elf(data.subspan(static_cast<std::size_t>(elf_start)))) {
        const int phnum = read_le16(data.data() + elf_start + 0x38);
        const int elf_len =
            static_cast<int>(SelfLayout::elf_header_size) +
            phnum * static_cast<int>(SelfLayout::elf_phdr_size);
        if (elf_start + elf_len <= static_cast<int>(data.size())) {
            image.elf.assign(
                data.begin() + elf_start,
                data.begin() + elf_start + elf_len);

            const int ext_start = align_up(elf_start + elf_len, 0x10);
            if (ext_start + static_cast<int>(SelfLayout::ext_info_size) <= image.header_size &&
                ext_start + static_cast<int>(SelfLayout::ext_info_size) <= static_cast<int>(data.size())) {
                SelfExtInfo ext;
                ext.authority_id = read_le64(data.data() + ext_start);
                ext.program_type = read_le64(data.data() + ext_start + 0x08);
                ext.app_version = read_le64(data.data() + ext_start + 0x10);
                ext.firmware_version = read_le64(data.data() + ext_start + 0x18);
                std::copy_n(data.begin() + ext_start + 0x20, ext.digest.size(), ext.digest.begin());
                image.ext_info = ext;
            }
        }
    }

    return image;
}

std::vector<std::byte> make_fself(std::span<const std::byte> elf, const FselfOptions& options)
{
    if (!is_elf(elf)) {
        throw std::invalid_argument("Input is not an ELF file.");
    }
    if (elf[4] != std::byte{2}) {
        throw std::invalid_argument("Only 64-bit ELF modules are supported.");
    }
    if (elf.size() < SelfLayout::elf_header_size) {
        throw std::invalid_argument("ELF header is truncated.");
    }

    const std::uint16_t e_type = read_le16(elf.data() + 0x10);
    const std::uint64_t phoff64 = read_le64(elf.data() + 0x20);
    const int phent_size = read_le16(elf.data() + 0x36);
    const int phnum = read_le16(elf.data() + 0x38);

    if (phoff64 > elf.size() || !fits_int(static_cast<std::size_t>(phoff64))) {
        throw std::invalid_argument("ELF program-header offset is outside the file.");
    }
    const int phoff = static_cast<int>(phoff64);
    if (phent_size != static_cast<int>(SelfLayout::elf_phdr_size)) {
        throw std::invalid_argument("Unexpected ELF program-header size.");
    }
    if (phnum < 0 ||
        static_cast<std::uint64_t>(phoff) +
            static_cast<std::uint64_t>(phnum) * SelfLayout::elf_phdr_size > elf.size()) {
        throw std::invalid_argument("ELF program headers overrun the file.");
    }

    const auto selected = select_segments(elf, phoff, phnum);
    if (selected.empty()) {
        throw std::invalid_argument("The ELF has no loadable segment content.");
    }

    const int segment_count = static_cast<int>(selected.size()) * 2;
    const int after_segments =
        static_cast<int>(SelfLayout::sce_header_size) +
        segment_count * static_cast<int>(SelfLayout::segment_entry_size);
    const int elf_header_len =
        static_cast<int>(SelfLayout::elf_header_size) +
        phnum * static_cast<int>(SelfLayout::elf_phdr_size);
    const int ext_info_start = align_up(after_segments + elf_header_len, 0x10);
    const int header_size =
        ext_info_start + static_cast<int>(SelfLayout::ext_info_size) + control_region_size;
    const int meta_size = meta_footer_base + (segment_count + 4) * 0x40;
    const int data_start = header_size + meta_size;

    std::vector<int> segment_offsets(segment_count);
    int cursor = data_start;
    for (std::size_t i = 0; i < selected.size(); ++i) {
        segment_offsets[i * 2] = cursor;
        cursor += digest_segment_size;
        segment_offsets[i * 2 + 1] = cursor;
        cursor = align_up(cursor + selected[i].file_size, 0x10);
    }
    const int file_size = cursor;

    std::vector<std::byte> buffer(static_cast<std::size_t>(file_size));
    std::span<std::byte> out(buffer.data(), buffer.size());

    write_le32(out.data(), SelfLayout::magic);
    out[0x04] = std::byte{0};
    out[0x05] = std::byte{1};
    out[0x06] = std::byte{1};
    out[0x07] = std::byte{0x12};
    write_le32(out.data() + 0x08, default_program_type);
    write_le16(out.data() + 0x0C, static_cast<std::uint16_t>(header_size));
    write_le16(out.data() + 0x0E, static_cast<std::uint16_t>(meta_size));
    write_le64(out.data() + 0x10, static_cast<std::uint64_t>(file_size));
    write_le16(out.data() + 0x18, static_cast<std::uint16_t>(segment_count));
    write_le16(out.data() + 0x1A, 0x0022);

    for (std::size_t i = 0; i < selected.size(); ++i) {
        const int digest_entry =
            static_cast<int>(SelfLayout::sce_header_size) +
            static_cast<int>(i * 2) * static_cast<int>(SelfLayout::segment_entry_size);
        const int data_entry = digest_entry + static_cast<int>(SelfLayout::segment_entry_size);
        const int data_table_index = static_cast<int>(i) * 2 + 1;

        const std::uint64_t digest_flags =
            (static_cast<std::uint64_t>(data_table_index) << 20u) | 0x10004u;
        write_segment(
            out,
            digest_entry,
            digest_flags,
            static_cast<std::uint64_t>(segment_offsets[i * 2]),
            digest_segment_size,
            digest_segment_size);

        const std::uint64_t data_flags =
            (static_cast<std::uint64_t>(selected[i].phdr_index) << 20u) | 0x2804u;
        write_segment(
            out,
            data_entry,
            data_flags,
            static_cast<std::uint64_t>(segment_offsets[i * 2 + 1]),
            static_cast<std::uint64_t>(selected[i].file_size),
            static_cast<std::uint64_t>(selected[i].file_size));
    }

    std::copy_n(elf.begin(), elf_header_len, out.begin() + after_segments);

    const std::uint64_t authority_id =
        options.authority_id.value_or(derive_authority_id(elf, e_type));
    write_le64(out.data() + ext_info_start, authority_id);
    write_le64(out.data() + ext_info_start + 0x08, 1);
    write_le64(out.data() + ext_info_start + 0x10, options.app_version);
    write_le64(out.data() + ext_info_start + 0x18, options.firmware_version);
    const auto digest = sha256(elf);
    std::copy(digest.begin(), digest.end(), out.begin() + ext_info_start + 0x20);

    write_le64(out.data() + ext_info_start + static_cast<int>(SelfLayout::ext_info_size), 3);

    if (meta_size > footer_marker_offset + 4) {
        write_le32(out.data() + header_size + footer_marker_offset, 0x00010000u);
    }

    for (std::size_t i = 0; i < selected.size(); ++i) {
        std::copy_n(
            elf.begin() + selected[i].file_offset,
            selected[i].file_size,
            out.begin() + segment_offsets[i * 2 + 1]);
    }

    return buffer;
}

} // namespace prosperopkg
