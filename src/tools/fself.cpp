// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/prosperopkg.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> read_all(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open input ELF: " + path.string());
    }

    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) {
        throw std::runtime_error("Could not determine input size: " + path.string());
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            throw std::runtime_error("Could not read input ELF: " + path.string());
        }
    }
    return data;
}

void write_all(const std::filesystem::path& path, std::span<const std::byte> data)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not open output SELF: " + path.string());
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file) {
        throw std::runtime_error("Could not write output SELF: " + path.string());
    }
}

[[nodiscard]] std::uint64_t parse_u64(std::string_view text)
{
    std::string value(text);
    std::size_t parsed = 0;
    std::uint64_t result = 0;
    try {
        result = std::stoull(value, &parsed, 0);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid integer option: " + value);
    }
    if (parsed != value.size()) {
        throw std::invalid_argument("Invalid integer option: " + value);
    }
    return result;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc != 3 && argc != 5) {
            std::cerr << "usage: prosperopkg-fself <input.elf> <output.self> [app-version firmware-version]\n";
            return 2;
        }

        prosperopkg::FselfOptions options;
        if (argc == 5) {
            options.app_version = parse_u64(argv[3]);
            options.firmware_version = parse_u64(argv[4]);
        }

        const auto elf = read_all(argv[1]);
        const auto self = prosperopkg::make_fself(elf, options);
        write_all(argv[2], self);
        std::cout << "Wrote fake SELF: " << argv[2] << " (" << self.size() << " bytes)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "prosperopkg-fself: " << ex.what() << '\n';
        return 1;
    }
}
