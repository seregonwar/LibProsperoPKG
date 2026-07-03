// LibProsperoPkg - A library for building and inspecting PS5 packages.
// Copyright (C) 2026 seregonwar.

#include <prosperopkg/lzn.hpp>
#include <prosperopkg/lzn_block.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Could not open input file: " + path.string());
    }
    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        throw std::runtime_error("Could not determine input file size: " + path.string());
    }
    file.seekg(0, std::ios::beg);

    std::vector<std::byte> data(static_cast<std::size_t>(end));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (file.gcount() != static_cast<std::streamsize>(data.size())) {
            throw std::runtime_error("Could not read input file: " + path.string());
        }
    }
    return data;
}

void write_file(const std::filesystem::path& path, std::span<const std::byte> data)
{
    const auto parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not create output file: " + path.string());
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        throw std::runtime_error("Could not write output file: " + path.string());
    }
}

[[nodiscard]] int parse_int(const std::string& text, int fallback)
{
    if (text.empty()) {
        return fallback;
    }
    std::size_t consumed = 0;
    const int value = std::stoi(text, &consumed, 10);
    if (consumed != text.size()) {
        throw std::invalid_argument("Invalid integer: " + text);
    }
    return value;
}

void print_usage(std::ostream& out, const char* argv0)
{
    out << "Usage:\n"
        << "  " << argv0 << " compress <input> <output> [level]\n"
        << "  " << argv0 << " decompress <input.lzn> <output>\n"
        << "  " << argv0 << " bench <input> [iterations] [level]\n"
        << "  " << argv0 << " block-compress <input> <output> [level] [block_size]\n"
        << "  " << argv0 << " block-decompress <input.lznb> <output>\n"
        << "  " << argv0 << " block-info <input.lznb>\n"
        << "  " << argv0 << " block-bench <input> [iterations] [level] [block_size]\n";
}

int compress_file(int argc, char** argv)
{
    if (argc < 4 || argc > 5) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    const int level = argc == 5 ? parse_int(argv[4], 1) : 1;
    const auto input = read_file(argv[2]);
    const auto compressed = prosperopkg::lzn_compress(input, level);
    write_file(argv[3], compressed);
    const auto info = prosperopkg::read_lzn_frame_info(compressed);
    const double ratio = input.empty() ? 1.0 : static_cast<double>(compressed.size()) / input.size();
    std::cout << "LZN1 compressed " << input.size() << " -> " << compressed.size()
              << " bytes, ratio=" << std::fixed << std::setprecision(3) << ratio
              << ", raw=" << (info.stored_raw() ? "yes" : "no") << '\n';
    return 0;
}

int decompress_file(int argc, char** argv)
{
    if (argc != 4) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    const auto input = read_file(argv[2]);
    const auto decompressed = prosperopkg::lzn_decompress(input);
    write_file(argv[3], decompressed);
    std::cout << "LZN1 decompressed " << input.size() << " -> " << decompressed.size() << " bytes\n";
    return 0;
}

int bench_file(int argc, char** argv)
{
    if (argc < 3 || argc > 5) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    const int iterations = argc >= 4 ? std::max(1, parse_int(argv[3], 20)) : 20;
    const int level = argc >= 5 ? parse_int(argv[4], 1) : 1;
    const auto input = read_file(argv[2]);

    std::vector<std::byte> compressed;
    std::vector<std::byte> decompressed;
    std::size_t sink = 0;

    auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index) {
        compressed = prosperopkg::lzn_compress(input, level);
        sink += compressed.size();
    }
    const auto compress_elapsed = std::chrono::steady_clock::now() - start;

    const auto info = prosperopkg::read_lzn_frame_info(compressed);
    decompressed.resize(static_cast<std::size_t>(info.original_size));

    start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index) {
        sink += prosperopkg::lzn_decompress_to(compressed, decompressed);
    }
    const auto decompress_elapsed = std::chrono::steady_clock::now() - start;

    const auto compress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(compress_elapsed).count();
    const auto decompress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(decompress_elapsed).count();
    const double mib = static_cast<double>(input.size()) / (1024.0 * 1024.0);
    const double compress_sec = static_cast<double>(compress_ns) / 1'000'000'000.0;
    const double decompress_sec = static_cast<double>(decompress_ns) / 1'000'000'000.0;
    const double ratio = input.empty() ? 1.0 : static_cast<double>(compressed.size()) / input.size();

    std::cout << "LZN1 benchmark\n"
              << "  input: " << input.size() << " bytes\n"
              << "  compressed: " << compressed.size() << " bytes\n"
              << "  ratio: " << std::fixed << std::setprecision(3) << ratio << '\n'
              << "  compress: " << std::setprecision(1) << (mib * iterations / compress_sec) << " MiB/s\n"
              << "  decompress: " << std::setprecision(1) << (mib * iterations / decompress_sec) << " MiB/s\n"
              << "  checksum: " << sink << '\n';
    return 0;
}

int block_compress_file(int argc, char** argv)
{
    if (argc < 4 || argc > 6) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    prosperopkg::LznBlockOptions options;
    options.level = argc >= 5 ? parse_int(argv[4], options.level) : options.level;
    options.block_size = argc >= 6
        ? static_cast<std::uint32_t>(std::max(1, parse_int(argv[5], static_cast<int>(options.block_size))))
        : options.block_size;

    const auto input = read_file(argv[2]);
    const auto compressed = prosperopkg::lzn_block_compress(input, options);
    write_file(argv[3], compressed);

    const auto info = prosperopkg::read_lzn_block_info(compressed);
    const double ratio = input.empty() ? 1.0 : static_cast<double>(compressed.size()) / input.size();
    std::cout << "LZNB compressed " << input.size() << " -> " << compressed.size()
              << " bytes, ratio=" << std::fixed << std::setprecision(3) << ratio
              << ", blocks=" << info.block_count
              << ", block_size=" << info.block_size << '\n';
    return 0;
}

int block_decompress_file(int argc, char** argv)
{
    if (argc != 4) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    const auto input = read_file(argv[2]);
    const auto decompressed = prosperopkg::lzn_block_decompress(input);
    write_file(argv[3], decompressed);
    std::cout << "LZNB decompressed " << input.size() << " -> " << decompressed.size() << " bytes\n";
    return 0;
}

int block_info_file(int argc, char** argv)
{
    if (argc != 3) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    const auto input = read_file(argv[2]);
    const auto info = prosperopkg::read_lzn_block_info(input);
    const auto entries = prosperopkg::read_lzn_block_entries(input);
    const double ratio = info.original_size == 0
        ? 1.0
        : static_cast<double>(info.archive_size) / static_cast<double>(info.original_size);
    const auto raw_blocks = std::count_if(entries.begin(), entries.end(), [](const auto& entry) {
        return entry.stored_raw();
    });

    std::cout << "LZNB archive\n"
              << "  version: " << info.version << '\n'
              << "  codec: " << (info.codec == prosperopkg::LznBlockCodec::lzn1 ? "lzn1" : "store") << '\n'
              << "  input: " << info.original_size << " bytes\n"
              << "  archive: " << info.archive_size << " bytes\n"
              << "  ratio: " << std::fixed << std::setprecision(3) << ratio << '\n'
              << "  block_size: " << info.block_size << '\n'
              << "  blocks: " << info.block_count << '\n'
              << "  raw_blocks: " << raw_blocks << '\n';
    return 0;
}

int block_bench_file(int argc, char** argv)
{
    if (argc < 3 || argc > 6) {
        print_usage(std::cerr, argv[0]);
        return 2;
    }
    const int iterations = argc >= 4 ? std::max(1, parse_int(argv[3], 20)) : 20;
    prosperopkg::LznBlockOptions options;
    options.level = argc >= 5 ? parse_int(argv[4], options.level) : options.level;
    options.block_size = argc >= 6
        ? static_cast<std::uint32_t>(std::max(1, parse_int(argv[5], static_cast<int>(options.block_size))))
        : options.block_size;

    const auto input = read_file(argv[2]);
    std::vector<std::byte> compressed;
    std::vector<std::byte> decompressed;
    std::size_t sink = 0;

    auto start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index) {
        compressed = prosperopkg::lzn_block_compress(input, options);
        sink += compressed.size();
    }
    const auto compress_elapsed = std::chrono::steady_clock::now() - start;

    const auto info = prosperopkg::read_lzn_block_info(compressed);
    decompressed.resize(static_cast<std::size_t>(info.original_size));

    start = std::chrono::steady_clock::now();
    for (int index = 0; index < iterations; ++index) {
        sink += prosperopkg::lzn_block_decompress_to(compressed, decompressed);
    }
    const auto decompress_elapsed = std::chrono::steady_clock::now() - start;

    const auto compress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(compress_elapsed).count();
    const auto decompress_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(decompress_elapsed).count();
    const double mib = static_cast<double>(input.size()) / (1024.0 * 1024.0);
    const double compress_sec = static_cast<double>(compress_ns) / 1'000'000'000.0;
    const double decompress_sec = static_cast<double>(decompress_ns) / 1'000'000'000.0;
    const double ratio = input.empty() ? 1.0 : static_cast<double>(compressed.size()) / input.size();

    std::cout << "LZNB benchmark\n"
              << "  input: " << input.size() << " bytes\n"
              << "  compressed: " << compressed.size() << " bytes\n"
              << "  ratio: " << std::fixed << std::setprecision(3) << ratio << '\n'
              << "  blocks: " << info.block_count << '\n'
              << "  block_size: " << info.block_size << '\n'
              << "  compress: " << std::setprecision(1) << (mib * iterations / compress_sec) << " MiB/s\n"
              << "  decompress: " << std::setprecision(1) << (mib * iterations / decompress_sec) << " MiB/s\n"
              << "  checksum: " << sink << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 2) {
            print_usage(std::cerr, argv[0]);
            return 2;
        }
        const std::string command = argv[1];
        if (command == "compress" || command == "c") {
            return compress_file(argc, argv);
        }
        if (command == "decompress" || command == "d") {
            return decompress_file(argc, argv);
        }
        if (command == "bench" || command == "b") {
            return bench_file(argc, argv);
        }
        if (command == "block-compress" || command == "bc") {
            return block_compress_file(argc, argv);
        }
        if (command == "block-decompress" || command == "bd") {
            return block_decompress_file(argc, argv);
        }
        if (command == "block-info" || command == "bi") {
            return block_info_file(argc, argv);
        }
        if (command == "block-bench" || command == "bb") {
            return block_bench_file(argc, argv);
        }
        print_usage(std::cerr, argv[0]);
        return 2;
    } catch (const std::exception& ex) {
        std::cerr << "prosperopkg-lzn: " << ex.what() << '\n';
        return 1;
    }
}
