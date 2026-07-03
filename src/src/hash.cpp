// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/hash.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace prosperopkg {
namespace {

[[nodiscard]] constexpr std::uint32_t rotr(std::uint32_t value, int bits) noexcept
{
    return (value >> bits) | (value << (32 - bits));
}

[[nodiscard]] constexpr std::uint64_t rotl64(std::uint64_t value, int bits) noexcept
{
    return bits == 0 ? value : ((value << bits) | (value >> (64 - bits)));
}

[[nodiscard]] constexpr std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
{
    return (x & y) ^ ((~x) & z);
}

[[nodiscard]] constexpr std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept
{
    return (x & y) ^ (x & z) ^ (y & z);
}

[[nodiscard]] constexpr std::uint32_t big_sigma0(std::uint32_t x) noexcept
{
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

[[nodiscard]] constexpr std::uint32_t big_sigma1(std::uint32_t x) noexcept
{
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

[[nodiscard]] constexpr std::uint32_t small_sigma0(std::uint32_t x) noexcept
{
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3u);
}

[[nodiscard]] constexpr std::uint32_t small_sigma1(std::uint32_t x) noexcept
{
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10u);
}

constexpr std::array<std::uint32_t, 64> k{
    0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
    0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
    0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
    0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
    0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
    0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
    0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
    0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
    0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
    0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
    0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
    0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
    0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
    0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
    0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
    0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u};

constexpr std::array<std::uint64_t, 24> keccak_round_constants{
    0x0000000000000001ull, 0x0000000000008082ull, 0x800000000000808Aull,
    0x8000000080008000ull, 0x000000000000808Bull, 0x0000000080000001ull,
    0x8000000080008081ull, 0x8000000000008009ull, 0x000000000000008Aull,
    0x0000000000000088ull, 0x0000000080008009ull, 0x000000008000000Aull,
    0x000000008000808Bull, 0x800000000000008Bull, 0x8000000000008089ull,
    0x8000000000008003ull, 0x8000000000008002ull, 0x8000000000000080ull,
    0x000000000000800Aull, 0x800000008000000Aull, 0x8000000080008081ull,
    0x8000000000008080ull, 0x0000000080000001ull, 0x8000000080008008ull};

constexpr std::array<int, 25> keccak_rho_offsets{
    0, 1, 62, 28, 27,
    36, 44, 6, 55, 20,
    3, 10, 43, 25, 39,
    41, 45, 15, 21, 8,
    18, 2, 61, 56, 14};

[[nodiscard]] std::uint64_t load_le64(const std::byte* data) noexcept
{
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[i])) << (i * 8);
    }
    return value;
}

void store_le64(std::byte* out, std::uint64_t value) noexcept
{
    for (int i = 0; i < 8; ++i) {
        out[i] = static_cast<std::byte>((value >> (i * 8)) & 0xFFu);
    }
}

void keccak_f1600(std::array<std::uint64_t, 25>& state) noexcept
{
    for (std::uint64_t rc : keccak_round_constants) {
        std::array<std::uint64_t, 5> c{};
        std::array<std::uint64_t, 5> d{};
        for (int x = 0; x < 5; ++x) {
            c[x] = state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
        }
        for (int x = 0; x < 5; ++x) {
            d[x] = c[(x + 4) % 5] ^ rotl64(c[(x + 1) % 5], 1);
        }
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                state[x + 5 * y] ^= d[x];
            }
        }

        std::array<std::uint64_t, 25> b{};
        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                b[y + 5 * ((2 * x + 3 * y) % 5)] =
                    rotl64(state[x + 5 * y], keccak_rho_offsets[x + 5 * y]);
            }
        }

        for (int y = 0; y < 5; ++y) {
            for (int x = 0; x < 5; ++x) {
                state[x + 5 * y] =
                    b[x + 5 * y] ^ ((~b[((x + 1) % 5) + 5 * y]) & b[((x + 2) % 5) + 5 * y]);
            }
        }
        state[0] ^= rc;
    }
}

} // namespace

std::array<std::byte, 32> sha256(std::span<const std::byte> data)
{
    std::vector<std::uint8_t> message;
    message.reserve(data.size() + 72);
    for (std::byte b : data) {
        message.push_back(static_cast<std::uint8_t>(b));
    }

    const std::uint64_t bit_len = static_cast<std::uint64_t>(message.size()) * 8u;
    message.push_back(0x80u);
    while ((message.size() % 64u) != 56u) {
        message.push_back(0);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bit_len >> shift) & 0xFFu));
    }

    std::array<std::uint32_t, 8> h{
        0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
        0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u};

    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64) {
        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            const std::size_t off = chunk + i * 4;
            w[i] = (static_cast<std::uint32_t>(message[off]) << 24u) |
                   (static_cast<std::uint32_t>(message[off + 1]) << 16u) |
                   (static_cast<std::uint32_t>(message[off + 2]) << 8u) |
                   static_cast<std::uint32_t>(message[off + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
        }

        std::uint32_t a = h[0];
        std::uint32_t b = h[1];
        std::uint32_t c = h[2];
        std::uint32_t d = h[3];
        std::uint32_t e = h[4];
        std::uint32_t f = h[5];
        std::uint32_t g = h[6];
        std::uint32_t hh = h[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t t1 = hh + big_sigma1(e) + ch(e, f, g) + k[i] + w[i];
            const std::uint32_t t2 = big_sigma0(a) + maj(a, b, c);
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }

        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }

    std::array<std::byte, 32> digest{};
    for (std::size_t i = 0; i < h.size(); ++i) {
        digest[i * 4] = static_cast<std::byte>((h[i] >> 24u) & 0xFFu);
        digest[i * 4 + 1] = static_cast<std::byte>((h[i] >> 16u) & 0xFFu);
        digest[i * 4 + 2] = static_cast<std::byte>((h[i] >> 8u) & 0xFFu);
        digest[i * 4 + 3] = static_cast<std::byte>(h[i] & 0xFFu);
    }
    return digest;
}

std::array<std::byte, 32> sha3_256(std::span<const std::byte> data)
{
    constexpr std::size_t rate = 136;
    std::array<std::uint64_t, 25> state{};

    while (data.size() >= rate) {
        for (std::size_t i = 0; i < rate / 8; ++i) {
            state[i] ^= load_le64(data.data() + i * 8);
        }
        keccak_f1600(state);
        data = data.subspan(rate);
    }

    std::array<std::byte, rate> block{};
    std::copy(data.begin(), data.end(), block.begin());
    block[data.size()] ^= std::byte{0x06};
    block[rate - 1] ^= std::byte{0x80};
    for (std::size_t i = 0; i < rate / 8; ++i) {
        state[i] ^= load_le64(block.data() + i * 8);
    }
    keccak_f1600(state);

    std::array<std::byte, 32> digest{};
    for (std::size_t i = 0; i < digest.size() / 8; ++i) {
        store_le64(digest.data() + i * 8, state[i]);
    }
    return digest;
}

std::array<std::byte, 32> hmac_sha256(std::span<const std::byte> key, std::span<const std::byte> data)
{
    constexpr std::size_t block_size = 64;
    std::array<std::byte, block_size> key_block{};

    if (key.size() > block_size) {
        const auto hashed = sha256(key);
        std::copy(hashed.begin(), hashed.end(), key_block.begin());
    } else {
        std::copy(key.begin(), key.end(), key_block.begin());
    }

    std::array<std::byte, block_size> inner_pad{};
    std::array<std::byte, block_size> outer_pad{};
    for (std::size_t i = 0; i < block_size; ++i) {
        inner_pad[i] = key_block[i] ^ std::byte{0x36};
        outer_pad[i] = key_block[i] ^ std::byte{0x5C};
    }

    std::vector<std::byte> inner;
    inner.reserve(block_size + data.size());
    inner.insert(inner.end(), inner_pad.begin(), inner_pad.end());
    inner.insert(inner.end(), data.begin(), data.end());
    const auto inner_digest = sha256(inner);

    std::vector<std::byte> outer;
    outer.reserve(block_size + inner_digest.size());
    outer.insert(outer.end(), outer_pad.begin(), outer_pad.end());
    outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
    return sha256(outer);
}

} // namespace prosperopkg
