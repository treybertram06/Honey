#include "hnpch.h"
#include "project_template.h"

#include "Honey/core/game_manifest.h"
#include "Honey/scene/scene.h"
#include "Honey/scene/scene_serializer.h"

#include <yaml-cpp/yaml.h>
#include <fstream>

namespace Honey {

    namespace fs = std::filesystem;

    namespace {
        constexpr const char* k_csproj = R"(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <AssemblyName>UserScripts</AssemblyName>
    <Nullable>enable</Nullable>
    <AllowUnsafeBlocks>true</AllowUnsafeBlocks>
    <Optimize>false</Optimize>
  </PropertyGroup>
  <ItemGroup>
    <!-- The editor passes -p:HoneyEngineDll=<path to HoneyEngine.dll> when it builds this project. -->
    <Reference Include="HoneyEngine">
      <HintPath>$(HoneyEngineDll)</HintPath>
    </Reference>
  </ItemGroup>
</Project>
)";

        constexpr const char* k_gitignore = "Cache/\nScripts/bin/\nScripts/obj/\n";

        bool write_text(const fs::path& path, const std::string& text, std::string& error) {
            std::ofstream out(path, std::ios::binary);
            if (!out || !(out << text)) {
                error = "could not write " + path.string();
                return false;
            }
            return true;
        }
    }

    bool ProjectTemplate::is_valid_name(const std::string& name, std::string& why_not) {
        if (name.empty()) { why_not = "name is empty"; return false; }
        if (name.front() == ' ' || name.back() == ' ') { why_not = "name cannot start or end with a space"; return false; }
        for (unsigned char c : name) {
            if (!(std::isalnum(c) || c == ' ' || c == '_' || c == '-')) {
                why_not = "use only letters, digits, spaces, '_' and '-'";
                return false;
            }
        }
        return true;
    }

    std::optional<fs::path> ProjectTemplate::create(const fs::path& parent_dir, const std::string& name, std::string& error) {
        HN_PROFILE_FUNCTION();
        if (!is_valid_name(name, error))
            return std::nullopt;

        std::error_code ec;
        if (!fs::is_directory(parent_dir, ec)) {
            error = "location is not a folder: " + parent_dir.string();
            return std::nullopt;
        }

        const fs::path root = parent_dir / name;
        if (fs::exists(root, ec) && !fs::is_empty(root, ec)) {
            error = "folder already exists and is not empty: " + root.string();
            return std::nullopt;
        }

        for (const char* dir : { "Assets/scenes", "Assets/prefabs", "Assets/models", "Assets/textures",
                                 "Assets/audio", "Assets/fonts", "Scripts" }) {
            fs::create_directories(root / dir, ec);
            if (ec) {
                error = "could not create " + (root / dir).string() + ": " + ec.message();
                return std::nullopt;
            }
        }

        // Start scene: an empty scene serialized through the normal path so the format stays in one place.
        const std::string start_scene = "scenes/main.hns";
        {
            Ref<Scene> scene = CreateRef<Scene>();
            SceneSerializer serializer(scene);
            serializer.serialize(root / "Assets" / start_scene);
            if (!fs::exists(root / "Assets" / start_scene, ec)) {
                error = "could not write the start scene";
                return std::nullopt;
            }
        }

        // .honeyproj (same schema GameManifest parses). Emitter handles quoting of the name.
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Project" << YAML::Value << YAML::BeginMap
            << YAML::Key << "EngineVersion" << YAML::Value << YAML::DoubleQuoted << GameManifest::k_engine_version
            << YAML::EndMap;
        out << YAML::Key << "Game" << YAML::Value << YAML::BeginMap
            << YAML::Key << "Name" << YAML::Value << name
            << YAML::Key << "StartScene" << YAML::Value << start_scene
            << YAML::Key << "Scenes" << YAML::Value << YAML::BeginSeq << start_scene << YAML::EndSeq
            << YAML::EndMap;
        out << YAML::Key << "Window" << YAML::Value << YAML::BeginMap
            << YAML::Key << "Title" << YAML::Value << name
            << YAML::Key << "Width" << YAML::Value << 1280
            << YAML::Key << "Height" << YAML::Value << 720
            << YAML::Key << "Mode" << YAML::Value << "windowed"
            << YAML::Key << "VSync" << YAML::Value << true
            << YAML::EndMap;
        out << YAML::EndMap;

        const fs::path project_file = root / (name + ".honeyproj");
        if (!write_text(project_file, std::string(out.c_str()) + "\n", error)
            || !write_text(root / "Scripts" / "UserScripts.csproj", k_csproj, error)
            || !write_text(root / ".gitignore", k_gitignore, error))
            return std::nullopt;

        HN_CORE_INFO("ProjectTemplate: created project '{}' at {}", name, root.string());
        return project_file;
    }

}
