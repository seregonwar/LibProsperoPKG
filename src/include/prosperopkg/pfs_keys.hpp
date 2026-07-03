// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace prosperopkg {

enum class PackageKeyDigest {
    sha256,
    sha3_256,
};

struct PfsEncryptionKeys {
    std::array<std::byte, 16> tweak_key{};
    std::array<std::byte, 16> data_key{};
};

[[nodiscard]] std::array<std::byte, 32> compute_package_key(
    std::string_view content_id,
    std::string_view passcode,
    std::uint32_t index,
    PackageKeyDigest digest = PackageKeyDigest::sha256);

[[nodiscard]] std::array<std::byte, 32> derive_ekpfs(
    std::string_view content_id,
    std::string_view passcode);

[[nodiscard]] std::array<std::byte, 32> pfs_gen_crypto_key(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed,
    std::uint32_t index);

[[nodiscard]] PfsEncryptionKeys derive_pfs_encryption_keys(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed,
    bool new_crypt = false);

[[nodiscard]] std::array<std::byte, 32> derive_pfs_sign_key(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed,
    bool new_crypt = false);

[[nodiscard]] PfsEncryptionKeys derive_image_encryption_keys(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed);

[[nodiscard]] PfsEncryptionKeys derive_image_encryption_keys(
    std::string_view content_id,
    std::string_view passcode,
    std::span<const std::byte> seed);

[[nodiscard]] std::array<std::byte, 32> derive_image_sign_key(
    std::span<const std::byte> ekpfs,
    std::span<const std::byte> seed);

[[nodiscard]] std::array<std::byte, 32> derive_image_sign_key(
    std::string_view content_id,
    std::string_view passcode,
    std::span<const std::byte> seed);

} // namespace prosperopkg
