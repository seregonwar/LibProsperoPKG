// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/pfs_keys.hpp>

#include <prosperopkg/hash.hpp>

#include <algorithm>
#include <stdexcept>

namespace prosperopkg {
namespace {

constexpr std::size_t content_id_length = 36;
constexpr std::size_t passcode_length = 32;
constexpr std::size_t padded_content_id_length = 48;
constexpr std::size_t ekpfs_length = 32;
constexpr std::size_t seed_length = 16;

[[nodiscard]] std::array<std::byte, 32> digest_bytes(
    std::span<const std::byte> data,
    PackageKeyDigest digest)
{
    switch (digest) {
    case PackageKeyDigest::sha256:
        return sha256(data);
    case PackageKeyDigest::sha3_256:
        return sha3_256(data);
    }
    throw std::invalid_argument("Unknown package key digest");
}

void validate_content_id(std::string_view content_id)
{
    if (content_id.size() != content_id_length) {
        throw std::invalid_argument("Content ID must be exactly 36 characters");
    }
}

void validate_passcode(std::string_view passcode)
{
    if (passcode.size() != passcode_length) {
        throw std::invalid_argument("Passcode must be exactly 32 characters");
    }
}

void validate_ekpfs(std::span<const std::byte> ekpfs)
{
    if (ekpfs.size() != ekpfs_length) {
        throw std::invalid_argument("EKPFS must be exactly 32 bytes");
    }
}

void validate_seed(std::span<const std::byte> seed)
{
    if (seed.size() != seed_length) {
        throw std::invalid_argument("PFS seed must be exactly 16 bytes");
    }
}

[[nodiscard]] std::array<std::byte, 4> be32(std::uint32_t value) noexcept
{
    return {
        static_cast<std::byte>((value >> 24u) & 0xFFu),
        static_cast<std::byte>((value >> 16u) & 0xFFu),
        static_cast<std::byte>((value >> 8u) & 0xFFu),
        static_cast<std::byte>(value & 0xFFu),
    };
}

[[nodiscard]] std::array<std::byte, 4> le32(std::uint32_t value) noexcept
{
    return {
        static_cast<std::byte>(value & 0xFFu),
        static_cast<std::byte>((value >> 8u) & 0xFFu),
        static_cast<std::byte>((value >> 16u) & 0xFFu),
        static_cast<std::byte>((value >> 24u) & 0xFFu),
    };
}

[[nodiscard]] std::array<std::byte, 32> maybe_new_crypt_key(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed,
    bool new_crypt)
{
    validate_ekpfs(ekpfs);
    validate_seed(seed);
    if (new_crypt) {
        return hmac_sha256(ekpfs, seed);
    }

    std::array<std::byte, 32> base_key{};
    std::copy(ekpfs.begin(), ekpfs.end(), base_key.begin());
    return base_key;
}

} // namespace

std::array<std::byte, 32> compute_package_key(
    std::string_view content_id,
    std::string_view passcode,
    std::uint32_t index,
    PackageKeyDigest digest)
{
    validate_content_id(content_id);
    validate_passcode(passcode);

    const auto index_be = be32(index);
    std::array<std::byte, padded_content_id_length> padded_content_id{};
    std::transform(content_id.begin(), content_id.end(), padded_content_id.begin(), [](char ch) {
        return static_cast<std::byte>(static_cast<unsigned char>(ch));
    });

    std::array<std::byte, 96> data{};
    const auto index_hash = digest_bytes(index_be, digest);
    const auto content_hash = digest_bytes(padded_content_id, digest);
    std::copy(index_hash.begin(), index_hash.end(), data.begin());
    std::copy(content_hash.begin(), content_hash.end(), data.begin() + 32);
    std::transform(passcode.begin(), passcode.end(), data.begin() + 64, [](char ch) {
        return static_cast<std::byte>(static_cast<unsigned char>(ch));
    });

    return digest_bytes(data, digest);
}

std::array<std::byte, 32> derive_ekpfs(std::string_view content_id, std::string_view passcode)
{
    return compute_package_key(content_id, passcode, 1, PackageKeyDigest::sha3_256);
}

std::array<std::byte, 32> pfs_gen_crypto_key(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed,
    std::uint32_t index)
{
    validate_ekpfs(ekpfs);
    validate_seed(seed);

    std::array<std::byte, 4 + seed_length> message{};
    const auto index_le = le32(index);
    std::copy(index_le.begin(), index_le.end(), message.begin());
    std::copy(seed.begin(), seed.end(), message.begin() + 4);
    return hmac_sha256(ekpfs, message);
}

PfsEncryptionKeys derive_pfs_encryption_keys(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed,
    bool new_crypt)
{
    const auto base_key = maybe_new_crypt_key(ekpfs, seed, new_crypt);
    const auto enc_key = pfs_gen_crypto_key(base_key, seed, 1);

    PfsEncryptionKeys keys{};
    std::copy(enc_key.begin(), enc_key.begin() + 16, keys.tweak_key.begin());
    std::copy(enc_key.begin() + 16, enc_key.end(), keys.data_key.begin());
    return keys;
}

std::array<std::byte, 32> derive_pfs_sign_key(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed,
    bool new_crypt)
{
    const auto base_key = maybe_new_crypt_key(ekpfs, seed, new_crypt);
    return pfs_gen_crypto_key(base_key, seed, 2);
}

PfsEncryptionKeys derive_image_encryption_keys(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed)
{
    return derive_pfs_encryption_keys(ekpfs, seed, true);
}

PfsEncryptionKeys derive_image_encryption_keys(
    std::string_view content_id,
    std::string_view passcode,
    std::span<const std::byte> seed)
{
    return derive_image_encryption_keys(derive_ekpfs(content_id, passcode), seed);
}

std::array<std::byte, 32> derive_image_sign_key(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed)
{
    return derive_pfs_sign_key(ekpfs, seed, true);
}

std::array<std::byte, 32> derive_image_sign_key(
    std::string_view content_id,
    std::string_view passcode,
    std::span<const std::byte> seed)
{
    return derive_image_sign_key(derive_ekpfs(content_id, passcode), seed);
}

} // namespace prosperopkg
