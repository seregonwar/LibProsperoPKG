// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace prosperopkg {

enum class Gp5VolumeType {
    prospero_app,
    prospero_patch,
    prospero_ac,
    prospero_ac_nodata,
};

enum class Gp5Layout {
    normal,
    flat,
};

struct Gp5Package {
    std::optional<std::string> content_id;
    std::string passcode = "00000000000000000000000000000000";
    std::optional<std::string> storage_type;
    std::optional<std::string> app_path;
};

struct Gp5Chunk {
    int id = 0;
    std::string label;
};

struct Gp5Scenario {
    int id = 0;
    std::string type = "playmode";
    int initial_chunk_count = 0;
    std::string label;
    std::string chunks = "0";
};

struct Gp5ChunkInfo {
    int chunk_count = 0;
    int scenario_count = 0;
    std::vector<Gp5Chunk> chunks;
    int default_scenario_id = 0;
    std::vector<Gp5Scenario> scenarios;
};

struct Gp5Volume {
    Gp5VolumeType type = Gp5VolumeType::prospero_app;
    std::optional<std::string> volume_id;
    std::optional<std::string> volume_timestamp;
    Gp5Package package;
    std::optional<Gp5ChunkInfo> chunk_info;
};

struct Gp5RootDir {
    std::optional<std::string> dir_exclude;
    std::optional<std::string> file_exclude;
    std::optional<std::string> source_path;
};

struct Gp5File {
    std::string destination_path;
    std::string source_path;
};

struct Gp5Dir {
    std::string destination_path;
    std::string source_path;
};

struct Gp5Project {
    std::string format = "gp5";
    int version = 1000;
    Gp5Volume volume;
    std::string global_exclude;
    Gp5RootDir root_dir;
    std::vector<Gp5File> files;
    std::vector<Gp5Dir> folders;

    [[nodiscard]] Gp5Layout layout() const noexcept;
};

inline constexpr const char* gp5_default_dir_exclude = "about";
inline constexpr const char* gp5_default_file_exclude =
    "*.gp5;*.esbak;keystone;*.dds;disc_info.dat;pfs-version.dat;ext_info.dat";

[[nodiscard]] const char* to_string(Gp5VolumeType type) noexcept;

[[nodiscard]] Gp5Project create_gp5_project(
    Gp5VolumeType type,
    std::string passcode = "00000000000000000000000000000000");

[[nodiscard]] Gp5Project gp5_from_folder(
    const std::filesystem::path& source_folder,
    Gp5VolumeType type = Gp5VolumeType::prospero_app,
    std::string passcode = "00000000000000000000000000000000",
    std::optional<std::string> root_dir_path_override = std::nullopt);

[[nodiscard]] Gp5Project gp5_from_folder_explicit(
    const std::filesystem::path& source_folder,
    Gp5VolumeType type = Gp5VolumeType::prospero_app,
    std::string passcode = "00000000000000000000000000000000");

[[nodiscard]] std::string gp5_to_xml(const Gp5Project& project);
void write_gp5(const Gp5Project& project, std::ostream& stream);
void write_gp5_file(const Gp5Project& project, const std::filesystem::path& path);

} // namespace prosperopkg
