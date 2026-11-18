#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace Honey {

    // Generates the skeleton of a new, portable project (see todos/sample_game_plan/phase_1_5_projects.md):
    //   <parent>/<name>/{<name>.honeyproj, .gitignore, Assets/{scenes,prefabs,models,textures,audio,fonts}, Scripts/UserScripts.csproj}
    // plus an empty start scene. Nothing absolute and nothing engine-checkout-relative is written.
    class ProjectTemplate {
    public:
        // Letters, digits, space, '_' and '-' only: the name becomes a folder and a file name on every OS.
        static bool is_valid_name(const std::string& name, std::string& why_not);

        // Returns the new .honeyproj path, or nullopt with `error` set. Fails if <parent>/<name> exists
        // and is not empty. Creating the empty start scene needs no open project.
        static std::optional<std::filesystem::path> create(const std::filesystem::path& parent_dir,
                                                           const std::string& name, std::string& error);
    };

}
