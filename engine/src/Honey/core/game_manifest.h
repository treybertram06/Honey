#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "settings.h"

namespace Honey {

    class GameManifest {
    public:
        GameManifest() = default;

        static bool load(const std::filesystem::path& path);
        static const GameManifest* get();
        static void apply_to(EngineSettings& settings);

    public:
        static constexpr const char* k_engine_version = "0.1";

        std::string name;
        std::string engine_version;   // version the project was last saved with (empty = unknown)
        std::string start_scene;
        std::vector<std::string> scenes;

        WindowMode window_mode = WindowMode::windowed;
        std::string window_title = "Honey Game";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool vsync = true;
    };

}
