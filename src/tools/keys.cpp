// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/prosperopkg.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] int hex_value(char ch)
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

[[nodiscard]] std::vector<std::byte> parse_hex(std::string_view text)
{
    std::vector<std::byte> out;
    std::string compact;
    compact.reserve(text.size());
    for (char ch : text) {
        if (ch == ':' || ch == ' ' || ch == '-' || ch == '_') {
            continue;
        }
        compact.push_back(ch);
    }

    if ((compact.size() % 2u) != 0u) {
        throw std::invalid_argument("Hex input must contain an even number of digits");
    }

    out.reserve(compact.size() / 2);
    for (std::size_t i = 0; i < compact.size(); i += 2) {
        const int hi = hex_value(compact[i]);
        const int lo = hex_value(compact[i + 1]);
        if (hi < 0 || lo < 0) {
            throw std::invalid_argument("Hex input contains a non-hex digit");
        }
        out.push_back(static_cast<std::byte>((hi << 4) | lo));
    }
    return out;
}

void print_hex(std::string_view label, std::span<const std::byte> data)
{
    std::cout << label << ": ";
    for (std::byte b : data) {
        std::cout << std::hex << std::setw(2) << std::setfill('0')
                  << static_cast<int>(static_cast<unsigned char>(b));
    }
    std::cout << std::dec << std::setfill(' ') << '\n';
}

void usage()
{
    std::cerr << "usage: prosperopkg-keys <content-id> <passcode> <seed-hex>\n";
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 4) {
            usage();
            return 2;
        }

        const std::string_view content_id = argv[1];
        const std::string_view passcode = argv[2];
        const auto seed_vec = parse_hex(argv[3]);
        if (seed_vec.size() != 16) {
            throw std::invalid_argument("PFS seed must decode to exactly 16 bytes");
        }

        std::array<std::byte, 16> seed{};
        std::copy(seed_vec.begin(), seed_vec.end(), seed.begin());

        const auto ekpfs = prosperopkg::derive_ekpfs(content_id, passcode);
        const auto keys = prosperopkg::derive_image_encryption_keys(ekpfs, seed);
        const auto sign_key = prosperopkg::derive_image_sign_key(ekpfs, seed);

        print_hex("EKPFS", ekpfs);
        print_hex("Tweak key", keys.tweak_key);
        print_hex("Data key", keys.data_key);
        print_hex("Sign key", sign_key);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "prosperopkg-keys: " << ex.what() << '\n';
        return 1;
    }
}
