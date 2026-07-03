// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/aes_xts.hpp>

#include <algorithm>
#include <array>
#include <stdexcept>
#include <string>

namespace prosperopkg {
namespace {

constexpr std::array<std::uint8_t, 256> sbox{
    0x63, 0x7C, 0x77, 0x7B, 0xF2, 0x6B, 0x6F, 0xC5, 0x30, 0x01, 0x67, 0x2B, 0xFE, 0xD7, 0xAB, 0x76,
    0xCA, 0x82, 0xC9, 0x7D, 0xFA, 0x59, 0x47, 0xF0, 0xAD, 0xD4, 0xA2, 0xAF, 0x9C, 0xA4, 0x72, 0xC0,
    0xB7, 0xFD, 0x93, 0x26, 0x36, 0x3F, 0xF7, 0xCC, 0x34, 0xA5, 0xE5, 0xF1, 0x71, 0xD8, 0x31, 0x15,
    0x04, 0xC7, 0x23, 0xC3, 0x18, 0x96, 0x05, 0x9A, 0x07, 0x12, 0x80, 0xE2, 0xEB, 0x27, 0xB2, 0x75,
    0x09, 0x83, 0x2C, 0x1A, 0x1B, 0x6E, 0x5A, 0xA0, 0x52, 0x3B, 0xD6, 0xB3, 0x29, 0xE3, 0x2F, 0x84,
    0x53, 0xD1, 0x00, 0xED, 0x20, 0xFC, 0xB1, 0x5B, 0x6A, 0xCB, 0xBE, 0x39, 0x4A, 0x4C, 0x58, 0xCF,
    0xD0, 0xEF, 0xAA, 0xFB, 0x43, 0x4D, 0x33, 0x85, 0x45, 0xF9, 0x02, 0x7F, 0x50, 0x3C, 0x9F, 0xA8,
    0x51, 0xA3, 0x40, 0x8F, 0x92, 0x9D, 0x38, 0xF5, 0xBC, 0xB6, 0xDA, 0x21, 0x10, 0xFF, 0xF3, 0xD2,
    0xCD, 0x0C, 0x13, 0xEC, 0x5F, 0x97, 0x44, 0x17, 0xC4, 0xA7, 0x7E, 0x3D, 0x64, 0x5D, 0x19, 0x73,
    0x60, 0x81, 0x4F, 0xDC, 0x22, 0x2A, 0x90, 0x88, 0x46, 0xEE, 0xB8, 0x14, 0xDE, 0x5E, 0x0B, 0xDB,
    0xE0, 0x32, 0x3A, 0x0A, 0x49, 0x06, 0x24, 0x5C, 0xC2, 0xD3, 0xAC, 0x62, 0x91, 0x95, 0xE4, 0x79,
    0xE7, 0xC8, 0x37, 0x6D, 0x8D, 0xD5, 0x4E, 0xA9, 0x6C, 0x56, 0xF4, 0xEA, 0x65, 0x7A, 0xAE, 0x08,
    0xBA, 0x78, 0x25, 0x2E, 0x1C, 0xA6, 0xB4, 0xC6, 0xE8, 0xDD, 0x74, 0x1F, 0x4B, 0xBD, 0x8B, 0x8A,
    0x70, 0x3E, 0xB5, 0x66, 0x48, 0x03, 0xF6, 0x0E, 0x61, 0x35, 0x57, 0xB9, 0x86, 0xC1, 0x1D, 0x9E,
    0xE1, 0xF8, 0x98, 0x11, 0x69, 0xD9, 0x8E, 0x94, 0x9B, 0x1E, 0x87, 0xE9, 0xCE, 0x55, 0x28, 0xDF,
    0x8C, 0xA1, 0x89, 0x0D, 0xBF, 0xE6, 0x42, 0x68, 0x41, 0x99, 0x2D, 0x0F, 0xB0, 0x54, 0xBB, 0x16};

constexpr std::array<std::uint8_t, 256> inv_sbox{
    0x52, 0x09, 0x6A, 0xD5, 0x30, 0x36, 0xA5, 0x38, 0xBF, 0x40, 0xA3, 0x9E, 0x81, 0xF3, 0xD7, 0xFB,
    0x7C, 0xE3, 0x39, 0x82, 0x9B, 0x2F, 0xFF, 0x87, 0x34, 0x8E, 0x43, 0x44, 0xC4, 0xDE, 0xE9, 0xCB,
    0x54, 0x7B, 0x94, 0x32, 0xA6, 0xC2, 0x23, 0x3D, 0xEE, 0x4C, 0x95, 0x0B, 0x42, 0xFA, 0xC3, 0x4E,
    0x08, 0x2E, 0xA1, 0x66, 0x28, 0xD9, 0x24, 0xB2, 0x76, 0x5B, 0xA2, 0x49, 0x6D, 0x8B, 0xD1, 0x25,
    0x72, 0xF8, 0xF6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xD4, 0xA4, 0x5C, 0xCC, 0x5D, 0x65, 0xB6, 0x92,
    0x6C, 0x70, 0x48, 0x50, 0xFD, 0xED, 0xB9, 0xDA, 0x5E, 0x15, 0x46, 0x57, 0xA7, 0x8D, 0x9D, 0x84,
    0x90, 0xD8, 0xAB, 0x00, 0x8C, 0xBC, 0xD3, 0x0A, 0xF7, 0xE4, 0x58, 0x05, 0xB8, 0xB3, 0x45, 0x06,
    0xD0, 0x2C, 0x1E, 0x8F, 0xCA, 0x3F, 0x0F, 0x02, 0xC1, 0xAF, 0xBD, 0x03, 0x01, 0x13, 0x8A, 0x6B,
    0x3A, 0x91, 0x11, 0x41, 0x4F, 0x67, 0xDC, 0xEA, 0x97, 0xF2, 0xCF, 0xCE, 0xF0, 0xB4, 0xE6, 0x73,
    0x96, 0xAC, 0x74, 0x22, 0xE7, 0xAD, 0x35, 0x85, 0xE2, 0xF9, 0x37, 0xE8, 0x1C, 0x75, 0xDF, 0x6E,
    0x47, 0xF1, 0x1A, 0x71, 0x1D, 0x29, 0xC5, 0x89, 0x6F, 0xB7, 0x62, 0x0E, 0xAA, 0x18, 0xBE, 0x1B,
    0xFC, 0x56, 0x3E, 0x4B, 0xC6, 0xD2, 0x79, 0x20, 0x9A, 0xDB, 0xC0, 0xFE, 0x78, 0xCD, 0x5A, 0xF4,
    0x1F, 0xDD, 0xA8, 0x33, 0x88, 0x07, 0xC7, 0x31, 0xB1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xEC, 0x5F,
    0x60, 0x51, 0x7F, 0xA9, 0x19, 0xB5, 0x4A, 0x0D, 0x2D, 0xE5, 0x7A, 0x9F, 0x93, 0xC9, 0x9C, 0xEF,
    0xA0, 0xE0, 0x3B, 0x4D, 0xAE, 0x2A, 0xF5, 0xB0, 0xC8, 0xEB, 0xBB, 0x3C, 0x83, 0x53, 0x99, 0x61,
    0x17, 0x2B, 0x04, 0x7E, 0xBA, 0x77, 0xD6, 0x26, 0xE1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0C, 0x7D};

constexpr std::array<std::uint8_t, 10> rcon{0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36};

using AesBlock = std::array<std::uint8_t, aes_block_size>;
using RoundKeys = std::array<std::uint8_t, 176>;

[[nodiscard]] std::uint8_t byte_value(std::byte value) noexcept
{
    return static_cast<std::uint8_t>(value);
}

[[nodiscard]] std::uint8_t xtime(std::uint8_t value) noexcept
{
    return static_cast<std::uint8_t>((value << 1u) ^ ((value & 0x80u) ? 0x1Bu : 0x00u));
}

[[nodiscard]] std::uint8_t gf_mul(std::uint8_t a, std::uint8_t b) noexcept
{
    std::uint8_t result = 0;
    while (b != 0) {
        if ((b & 1u) != 0) {
            result ^= a;
        }
        a = xtime(a);
        b >>= 1u;
    }
    return result;
}

[[nodiscard]] AesBlock to_block(std::span<const std::byte> data, const char* name)
{
    if (data.size() != aes_block_size) {
        throw std::invalid_argument(std::string(name) + " must be exactly 16 bytes");
    }
    AesBlock out{};
    std::transform(data.begin(), data.end(), out.begin(), byte_value);
    return out;
}

[[nodiscard]] RoundKeys expand_key(std::span<const std::byte> key)
{
    if (key.size() != aes128_key_size) {
        throw std::invalid_argument("AES-128 key must be exactly 16 bytes");
    }

    RoundKeys round_keys{};
    std::transform(key.begin(), key.end(), round_keys.begin(), byte_value);

    std::array<std::uint8_t, 4> temp{};
    std::size_t bytes_generated = aes128_key_size;
    std::size_t rcon_index = 0;
    while (bytes_generated < round_keys.size()) {
        std::copy_n(round_keys.begin() + static_cast<std::ptrdiff_t>(bytes_generated - 4), 4, temp.begin());
        if ((bytes_generated % aes128_key_size) == 0) {
            const std::uint8_t first = temp[0];
            temp[0] = sbox[temp[1]] ^ rcon[rcon_index++];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[first];
        }

        for (std::uint8_t value : temp) {
            round_keys[bytes_generated] =
                static_cast<std::uint8_t>(round_keys[bytes_generated - aes128_key_size] ^ value);
            ++bytes_generated;
        }
    }
    return round_keys;
}

void add_round_key(AesBlock& state, const RoundKeys& keys, std::size_t round) noexcept
{
    const std::size_t offset = round * aes_block_size;
    for (std::size_t i = 0; i < aes_block_size; ++i) {
        state[i] ^= keys[offset + i];
    }
}

void sub_bytes(AesBlock& state) noexcept
{
    for (std::uint8_t& b : state) {
        b = sbox[b];
    }
}

void inv_sub_bytes(AesBlock& state) noexcept
{
    for (std::uint8_t& b : state) {
        b = inv_sbox[b];
    }
}

void shift_rows(AesBlock& state) noexcept
{
    AesBlock tmp = state;
    state[1] = tmp[5];   state[5] = tmp[9];   state[9] = tmp[13];  state[13] = tmp[1];
    state[2] = tmp[10];  state[6] = tmp[14];  state[10] = tmp[2];  state[14] = tmp[6];
    state[3] = tmp[15];  state[7] = tmp[3];   state[11] = tmp[7];  state[15] = tmp[11];
}

void inv_shift_rows(AesBlock& state) noexcept
{
    AesBlock tmp = state;
    state[1] = tmp[13];  state[5] = tmp[1];   state[9] = tmp[5];   state[13] = tmp[9];
    state[2] = tmp[10];  state[6] = tmp[14];  state[10] = tmp[2];  state[14] = tmp[6];
    state[3] = tmp[7];   state[7] = tmp[11];  state[11] = tmp[15]; state[15] = tmp[3];
}

void mix_columns(AesBlock& state) noexcept
{
    for (std::size_t c = 0; c < 4; ++c) {
        const std::size_t i = c * 4;
        const std::uint8_t a0 = state[i];
        const std::uint8_t a1 = state[i + 1];
        const std::uint8_t a2 = state[i + 2];
        const std::uint8_t a3 = state[i + 3];
        state[i] = static_cast<std::uint8_t>(gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3);
        state[i + 1] = static_cast<std::uint8_t>(a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3);
        state[i + 2] = static_cast<std::uint8_t>(a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3));
        state[i + 3] = static_cast<std::uint8_t>(gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2));
    }
}

void inv_mix_columns(AesBlock& state) noexcept
{
    for (std::size_t c = 0; c < 4; ++c) {
        const std::size_t i = c * 4;
        const std::uint8_t a0 = state[i];
        const std::uint8_t a1 = state[i + 1];
        const std::uint8_t a2 = state[i + 2];
        const std::uint8_t a3 = state[i + 3];
        state[i] = static_cast<std::uint8_t>(gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9));
        state[i + 1] = static_cast<std::uint8_t>(gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13));
        state[i + 2] = static_cast<std::uint8_t>(gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11));
        state[i + 3] = static_cast<std::uint8_t>(gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14));
    }
}

void xts_mul_alpha(AesBlock& tweak) noexcept
{
    int feedback = 0;
    for (std::uint8_t& b : tweak) {
        const std::uint8_t old = b;
        b = static_cast<std::uint8_t>((b << 1u) | feedback);
        feedback = (old & 0x80u) != 0 ? 1 : 0;
    }
    if (feedback != 0) {
        tweak[0] ^= 0x87u;
    }
}

[[nodiscard]] std::array<std::byte, aes_block_size> from_block(const AesBlock& block) noexcept
{
    std::array<std::byte, aes_block_size> out{};
    std::transform(block.begin(), block.end(), out.begin(), [](std::uint8_t b) {
        return static_cast<std::byte>(b);
    });
    return out;
}

} // namespace

std::array<std::byte, aes_block_size> aes128_encrypt_block(
    std::span<const std::byte> key,
    std::span<const std::byte> block)
{
    const auto round_keys = expand_key(key);
    AesBlock state = to_block(block, "AES block");

    add_round_key(state, round_keys, 0);
    for (std::size_t round = 1; round < 10; ++round) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, round_keys, round);
    }
    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, round_keys, 10);
    return from_block(state);
}

std::array<std::byte, aes_block_size> aes128_decrypt_block(
    std::span<const std::byte> key,
    std::span<const std::byte> block)
{
    const auto round_keys = expand_key(key);
    AesBlock state = to_block(block, "AES block");

    add_round_key(state, round_keys, 10);
    for (std::size_t round = 9; round > 0; --round) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, round_keys, round);
        inv_mix_columns(state);
    }
    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, round_keys, 0);
    return from_block(state);
}

void xts_transform_unit(
    std::span<std::byte> data_unit,
    std::span<const std::byte> data_key,
    std::span<const std::byte> tweak_key,
    std::uint64_t sector_number,
    bool encrypt)
{
    if (data_key.size() != aes128_key_size) {
        throw std::invalid_argument("AES-XTS data key must be exactly 16 bytes");
    }
    if (tweak_key.size() != aes128_key_size) {
        throw std::invalid_argument("AES-XTS tweak key must be exactly 16 bytes");
    }
    if ((data_unit.size() % aes_block_size) != 0) {
        throw std::invalid_argument("AES-XTS data unit length must be a multiple of 16 bytes");
    }

    std::array<std::byte, aes_block_size> tweak_plain{};
    for (std::size_t i = 0; i < 8; ++i) {
        tweak_plain[i] = static_cast<std::byte>((sector_number >> (i * 8u)) & 0xFFu);
    }
    AesBlock tweak = to_block(aes128_encrypt_block(tweak_key, tweak_plain), "AES block");

    for (std::size_t offset = 0; offset < data_unit.size(); offset += aes_block_size) {
        std::array<std::byte, aes_block_size> xored{};
        for (std::size_t i = 0; i < aes_block_size; ++i) {
            xored[i] = data_unit[offset + i] ^ static_cast<std::byte>(tweak[i]);
        }

        const auto crypted = encrypt
            ? aes128_encrypt_block(data_key, xored)
            : aes128_decrypt_block(data_key, xored);

        for (std::size_t i = 0; i < aes_block_size; ++i) {
            data_unit[offset + i] = crypted[i] ^ static_cast<std::byte>(tweak[i]);
        }
        xts_mul_alpha(tweak);
    }
}

} // namespace prosperopkg
