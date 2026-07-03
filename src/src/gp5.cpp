// LibProsperoPkg - A library for building and inspecting PS5 packages.
// C++ port/rewrite Copyright (C) 2026 seregonwar.
// Original C# LibProsperoPkg by SvenGDK.
// SPDX-License-Identifier: GPL-3.0-or-later

#include <prosperopkg/gp5.hpp>

#include <algorithm>
#include <fstream>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace prosperopkg {
namespace {

[[nodiscard]] std::string xml_escape(std::string_view value, bool attribute)
{
    std::string out;
    out.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += attribute ? "&quot;" : "\"";
            break;
        case '\'':
            out += attribute ? "&apos;" : "'";
            break;
        default:
            out.push_back(ch);
            break;
        }
    }
    return out;
}

void indent(std::ostream& out, int level)
{
    for (int i = 0; i < level; ++i) {
        out << "  ";
    }
}

void write_attr(std::ostream& out, std::string_view name, const std::string& value)
{
    out << ' ' << name << "=\"" << xml_escape(value, true) << '"';
}

void write_attr(std::ostream& out, std::string_view name, int value)
{
    out << ' ' << name << "=\"" << value << '"';
}

void write_optional_element(
    std::ostream& out,
    int level,
    std::string_view name,
    const std::optional<std::string>& value)
{
    if (!value || value->empty()) {
        return;
    }
    indent(out, level);
    out << '<' << name << '>' << xml_escape(*value, false) << "</" << name << ">\n";
}

void write_package(std::ostream& out, const Gp5Package& package)
{
    indent(out, 2);
    out << "<package";
    if (package.content_id && !package.content_id->empty()) {
        write_attr(out, "content_id", *package.content_id);
    }
    write_attr(out, "passcode", package.passcode);
    if (package.storage_type && !package.storage_type->empty()) {
        write_attr(out, "storage_type", *package.storage_type);
    }
    if (package.app_path && !package.app_path->empty()) {
        write_attr(out, "app_path", *package.app_path);
    }
    out << " />\n";
}

void write_chunk_info(std::ostream& out, const Gp5ChunkInfo& info)
{
    indent(out, 2);
    out << "<chunk_info";
    write_attr(out, "chunk_count", info.chunk_count);
    write_attr(out, "scenario_count", info.scenario_count);
    out << ">\n";

    indent(out, 3);
    out << "<chunks>\n";
    for (const auto& chunk : info.chunks) {
        indent(out, 4);
        out << "<chunk";
        write_attr(out, "id", chunk.id);
        write_attr(out, "label", chunk.label);
        out << " />\n";
    }
    indent(out, 3);
    out << "</chunks>\n";

    indent(out, 3);
    out << "<scenarios";
    write_attr(out, "default_id", info.default_scenario_id);
    out << ">\n";
    for (const auto& scenario : info.scenarios) {
        indent(out, 4);
        out << "<scenario";
        write_attr(out, "id", scenario.id);
        write_attr(out, "type", scenario.type);
        write_attr(out, "initial_chunk_count", scenario.initial_chunk_count);
        write_attr(out, "label", scenario.label);
        out << '>' << xml_escape(scenario.chunks, false) << "</scenario>\n";
    }
    indent(out, 3);
    out << "</scenarios>\n";

    indent(out, 2);
    out << "</chunk_info>\n";
}

void write_volume(std::ostream& out, const Gp5Volume& volume)
{
    indent(out, 1);
    out << "<volume>\n";

    indent(out, 2);
    out << "<volume_type>" << to_string(volume.type) << "</volume_type>\n";
    write_optional_element(out, 2, "volume_id", volume.volume_id);
    write_optional_element(out, 2, "volume_ts", volume.volume_timestamp);
    write_package(out, volume.package);
    if (volume.chunk_info) {
        write_chunk_info(out, *volume.chunk_info);
    }

    indent(out, 1);
    out << "</volume>\n";
}

void write_root_dir(std::ostream& out, const Gp5RootDir& root)
{
    indent(out, 1);
    out << "<rootdir";
    if (root.dir_exclude && !root.dir_exclude->empty()) {
        write_attr(out, "dir_exclude", *root.dir_exclude);
    }
    if (root.file_exclude && !root.file_exclude->empty()) {
        write_attr(out, "file_exclude", *root.file_exclude);
    }
    if (root.source_path && !root.source_path->empty()) {
        write_attr(out, "src_path", *root.source_path);
    }
    out << " />\n";
}

void write_files(std::ostream& out, const std::vector<Gp5File>& files)
{
    if (files.empty()) {
        return;
    }
    indent(out, 1);
    out << "<files>\n";
    for (const auto& file : files) {
        indent(out, 2);
        out << "<file";
        write_attr(out, "dst_path", file.destination_path);
        write_attr(out, "src_path", file.source_path);
        out << " />\n";
    }
    indent(out, 1);
    out << "</files>\n";
}

void write_folders(std::ostream& out, const std::vector<Gp5Dir>& folders)
{
    if (folders.empty()) {
        return;
    }
    indent(out, 1);
    out << "<folders>\n";
    for (const auto& folder : folders) {
        indent(out, 2);
        out << "<dir";
        write_attr(out, "dst_path", folder.destination_path);
        write_attr(out, "src_path", folder.source_path);
        out << " />\n";
    }
    indent(out, 1);
    out << "</folders>\n";
}

[[nodiscard]] std::string destination_path_from_relative(std::filesystem::path relative)
{
    std::string dst = relative.generic_string();
    std::replace(dst.begin(), dst.end(), '/', '\\');
    return dst;
}

[[nodiscard]] std::vector<std::filesystem::path> collect_files(const std::filesystem::path& root)
{
    std::vector<std::filesystem::path> files;
    for (const auto& item : std::filesystem::recursive_directory_iterator(root)) {
        if (item.is_regular_file()) {
            files.push_back(item.path());
        }
    }
    std::sort(files.begin(), files.end(), [](const auto& lhs, const auto& rhs) {
        return lhs.generic_string() < rhs.generic_string();
    });
    return files;
}

} // namespace

Gp5Layout Gp5Project::layout() const noexcept
{
    return (!files.empty() || !folders.empty()) ? Gp5Layout::flat : Gp5Layout::normal;
}

const char* to_string(Gp5VolumeType type) noexcept
{
    switch (type) {
    case Gp5VolumeType::prospero_app:
        return "prospero_app";
    case Gp5VolumeType::prospero_patch:
        return "prospero_patch";
    case Gp5VolumeType::prospero_ac:
        return "prospero_ac";
    case Gp5VolumeType::prospero_ac_nodata:
        return "prospero_ac_nodata";
    }
    return "prospero_app";
}

Gp5Project create_gp5_project(Gp5VolumeType type, std::string passcode)
{
    Gp5Project project;
    project.volume.type = type;
    project.volume.package.passcode = std::move(passcode);

    if (type == Gp5VolumeType::prospero_app || type == Gp5VolumeType::prospero_patch) {
        Gp5ChunkInfo chunk_info;
        chunk_info.chunk_count = 1;
        chunk_info.scenario_count = 1;
        chunk_info.chunks.push_back(Gp5Chunk{0, "Chunk #0"});
        chunk_info.default_scenario_id = 0;
        chunk_info.scenarios.push_back(Gp5Scenario{0, "playmode", 1, "Scenario #0", "0"});
        project.volume.chunk_info = std::move(chunk_info);
    }

    return project;
}

Gp5Project gp5_from_folder(
    const std::filesystem::path& source_folder,
    Gp5VolumeType type,
    std::string passcode,
    std::optional<std::string> root_dir_path_override)
{
    if (source_folder.empty() || !std::filesystem::is_directory(source_folder)) {
        throw std::runtime_error("Source folder does not exist: " + source_folder.string());
    }

    Gp5Project project = create_gp5_project(type, std::move(passcode));
    project.root_dir.source_path = root_dir_path_override
        ? std::move(root_dir_path_override)
        : std::optional<std::string>(source_folder.string());
    project.root_dir.dir_exclude = gp5_default_dir_exclude;
    project.root_dir.file_exclude = gp5_default_file_exclude;
    return project;
}

Gp5Project gp5_from_folder_explicit(
    const std::filesystem::path& source_folder,
    Gp5VolumeType type,
    std::string passcode)
{
    if (source_folder.empty() || !std::filesystem::is_directory(source_folder)) {
        throw std::runtime_error("Source folder does not exist: " + source_folder.string());
    }

    const auto root = std::filesystem::absolute(source_folder);
    Gp5Project project = create_gp5_project(type, std::move(passcode));
    for (const auto& file : collect_files(root)) {
        const auto relative = std::filesystem::relative(file, root);
        project.files.push_back(Gp5File{
            destination_path_from_relative(relative),
            file.string(),
        });
    }
    return project;
}

std::string gp5_to_xml(const Gp5Project& project)
{
    std::ostringstream out;
    write_gp5(project, out);
    return out.str();
}

void write_gp5(const Gp5Project& project, std::ostream& stream)
{
    stream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    stream << "<psproject";
    write_attr(stream, "fmt", project.format);
    write_attr(stream, "version", project.version);
    stream << ">\n";

    write_volume(stream, project.volume);
    if (project.layout() == Gp5Layout::normal) {
        indent(stream, 1);
        stream << "<global_exclude>" << xml_escape(project.global_exclude, false) << "</global_exclude>\n";
        write_root_dir(stream, project.root_dir);
    } else {
        write_files(stream, project.files);
        write_folders(stream, project.folders);
    }

    stream << "</psproject>\n";
}

void write_gp5_file(const Gp5Project& project, const std::filesystem::path& path)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        throw std::runtime_error("Could not open GP5 output path: " + path.string());
    }
    write_gp5(project, file);
    if (!file) {
        throw std::runtime_error("Could not write GP5 output path: " + path.string());
    }
}

} // namespace prosperopkg
