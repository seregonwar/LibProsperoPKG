#pragma once

#include <string>
#include <filesystem>
#include <vector>
#include <array>
#include <cstddef>

namespace prospero::gui {

enum class Screen {
    Home,
    InspectPkg,
    BuildPkg,
    PfscEditor,
    KeyDerivation,
};

struct WorkspaceFileEntry {
    std::string name;
    std::string size;
    std::string source;
};

struct WorkspaceDigest {
    std::string label;
    std::string value;
};

struct SharedWorkspace {
    std::string content_id;
    std::string title_id;
    std::string title;
    std::string passcode;
    int package_kind = 0;
    bool include_build_timestamp = false;

    std::string source_folder;
    std::string output_folder;
    int compression_index = 0;

    std::vector<WorkspaceFileEntry> file_entries;
    std::vector<WorkspaceDigest> digest_results;

    std::string loaded_pkg_path;
    bool pkg_loaded = false;
};

struct AppState {
    Screen current_screen = Screen::Home;
    std::string status_message = "Ready";
    float dpi_scale = 1.0f;

    std::filesystem::path last_directory;

    SharedWorkspace workspace;

    bool request_exit = false;
};

AppState& get_app_state();

}
