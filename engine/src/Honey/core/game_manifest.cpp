#include "hnpch.h"
#include "game_manifest.h"

#include "Honey/core/asset_paths.h"

#include <yaml-cpp/yaml.h>
#include <optional>

namespace Honey {

    namespace {
        std::optional<GameManifest> s_manifest;

        bool parse_window_mode(const std::string& str, WindowMode& out) {
            std::string s = str;
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });

            if (s == "windowed")   { out = WindowMode::windowed;   return true; }
            if (s == "maximized")  { out = WindowMode::maximized;  return true; }
            if (s == "fullscreen") { out = WindowMode::fullscreen; return true; }
            return false;
        }
    }

    bool GameManifest::load(const std::filesystem::path& path) {
        HN_PROFILE_FUNCTION();

        s_manifest.reset(); // A failed load must never leave a stale manifest behind.

        if (!std::filesystem::exists(path)) {
            HN_CORE_ERROR("Game manifest not found: {}", path.string());
            return false;
        }

        YAML::Node root;
        try {
            root = YAML::LoadFile(path.string());
        } catch (const YAML::Exception& e) {
            HN_CORE_ERROR("Failed to parse game manifest {}: {}", path.string(), e.what());
            return false;
        }

        GameManifest manifest;

        try {
            // ---------------- Game -----------------
            const auto game_node = root["Game"];
            if (!game_node) {
                HN_CORE_ERROR("Game manifest {} has no 'Game' section", path.string());
                return false;
            }

            if (auto n = game_node["Name"])
                manifest.name = n.as<std::string>(manifest.name);

            // Default window title to the game name; an explicit Window.Title below overrides it.
            if (!manifest.name.empty())
                manifest.window_title = manifest.name;

            const auto start_node = game_node["StartScene"];
            if (!start_node) {
                HN_CORE_ERROR("Game manifest {} has no 'Game.StartScene'", path.string());
                return false;
            }
            manifest.start_scene = start_node.as<std::string>("");

            if (auto n = game_node["Scenes"]) {
                for (const auto& scene : n)
                    manifest.scenes.push_back(scene.as<std::string>(""));
            }

            // ---------------- Window -----------------
            if (auto window_node = root["Window"]) {
                if (auto n = window_node["Title"])
                    manifest.window_title = n.as<std::string>(manifest.window_title);

                if (auto n = window_node["Width"])
                    manifest.width = n.as<uint32_t>(manifest.width);

                if (auto n = window_node["Height"])
                    manifest.height = n.as<uint32_t>(manifest.height);

                if (auto n = window_node["Mode"]) {
                    const auto mode_str = n.as<std::string>("");
                    if (!parse_window_mode(mode_str, manifest.window_mode))
                        HN_CORE_WARN("Game manifest: unknown Window.Mode '{}', using 'windowed'", mode_str);
                }

                if (auto n = window_node["VSync"])
                    manifest.vsync = n.as<bool>(manifest.vsync);
            }
        } catch (const YAML::Exception& e) {
            HN_CORE_ERROR("Invalid game manifest {}: {}", path.string(), e.what());
            return false;
        }

        // ---------------- Validation -----------------
        // The start scene is fatal if unusable; other listed scenes only warn (they may be WIP).
        const auto start_resolved = AssetPaths::resolve(manifest.start_scene);
        if (start_resolved.empty() || !std::filesystem::exists(start_resolved)) {
            HN_CORE_ERROR("Game manifest: StartScene '{}' does not resolve to an existing asset", manifest.start_scene);
            return false;
        }

        std::erase_if(manifest.scenes, [](const std::string& scene) {
            const auto resolved = AssetPaths::resolve(scene);
            if (resolved.empty() || !std::filesystem::exists(resolved)) {
                HN_CORE_WARN("Game manifest: scene '{}' is missing or invalid, dropping it from the scene list", scene);
                return true;
            }
            return false;
        });

        if (std::find(manifest.scenes.begin(), manifest.scenes.end(), manifest.start_scene) == manifest.scenes.end()) {
            HN_CORE_WARN("Game manifest: StartScene '{}' was not in Scenes, adding it", manifest.start_scene);
            manifest.scenes.insert(manifest.scenes.begin(), manifest.start_scene);
        }

        s_manifest = std::move(manifest);
        HN_CORE_INFO("Game manifest loaded from {} (start scene: {}, {} scene(s))",
                     path.string(), s_manifest->start_scene, s_manifest->scenes.size());
        return true;
    }

    const GameManifest* GameManifest::get() {
        return s_manifest ? &*s_manifest : nullptr;
    }

    void GameManifest::apply_to(EngineSettings& settings) {
        if (!s_manifest)
            return;

        const GameManifest& m = *s_manifest;

        settings.window.title = m.window_title;
        settings.window.width = m.width;
        settings.window.height = m.height;
        settings.window.mode = m.window_mode;

        settings.renderer.vsync = m.vsync;
    }

}
