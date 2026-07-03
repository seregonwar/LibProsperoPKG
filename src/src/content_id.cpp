// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/content_id.hpp>

#include <algorithm>

namespace prosperopkg {
namespace {

[[nodiscard]] char upper_ascii(char ch) noexcept
{
    if (ch >= 'a' && ch <= 'z') {
        return static_cast<char>(ch - 'a' + 'A');
    }
    return ch;
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

[[nodiscard]] std::string uppercase_trim_pad(
    std::string_view value,
    std::string_view fallback,
    std::size_t size,
    char pad)
{
    std::string out(value.empty() ? fallback : value);
    std::transform(out.begin(), out.end(), out.begin(), upper_ascii);
    if (out.size() < size) {
        out.append(size - out.size(), pad);
    }
    if (out.size() > size) {
        out.resize(size);
    }
    return out;
}

} // namespace

bool is_valid_content_id(std::string_view content_id)
{
    if (content_id.size() != 36) {
        return false;
    }

    return is_ascii_upper(content_id[0]) &&
           is_ascii_upper(content_id[1]) &&
           std::all_of(content_id.begin() + 2, content_id.begin() + 6, is_ascii_digit) &&
           content_id[6] == '-' &&
           std::all_of(content_id.begin() + 7, content_id.begin() + 11, is_ascii_upper) &&
           std::all_of(content_id.begin() + 11, content_id.begin() + 16, is_ascii_digit) &&
           content_id[16] == '_' &&
           content_id[17] == '0' &&
           content_id[18] == '0' &&
           content_id[19] == '-' &&
           std::all_of(content_id.begin() + 20, content_id.end(), is_ascii_alnum_upper);
}

bool is_valid_title_id(std::string_view title_id)
{
    return title_id.size() == 9 &&
           std::all_of(title_id.begin(), title_id.begin() + 4, is_ascii_upper) &&
           std::all_of(title_id.begin() + 4, title_id.end(), is_ascii_digit);
}

std::string compose_content_id(
    std::string_view publisher,
    std::string_view title_id,
    std::string_view label)
{
    std::string publisher_part = uppercase_trim_pad(publisher, "UP9000", 6, '0');
    std::string title_part = uppercase_trim_pad(title_id, "PPSA00000", 9, '0');

    std::string label_part;
    label_part.reserve(16);
    for (char ch : label) {
        char upper = upper_ascii(ch);
        if (is_ascii_alnum_upper(upper)) {
            label_part.push_back(upper);
        }
    }
    if (label_part.size() < 16) {
        label_part.append(16 - label_part.size(), '0');
    }
    if (label_part.size() > 16) {
        label_part.resize(16);
    }

    return publisher_part + "-" + title_part + "_00-" + label_part;
}

bool is_dlc_mode(PackageMode mode) noexcept
{
    return mode == PackageMode::additional_content_data ||
           mode == PackageMode::additional_content_no_data;
}

} // namespace prosperopkg
