#include "hnpch.h"
#include "settings.h"
#include "Honey/math/yaml_glm.h"

#include <yaml-cpp/yaml.h>

namespace Honey {
    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v);
    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v);
    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v);

    static RendererAPI::API parse_renderer_api(const std::string& api_str, RendererAPI::API fallback) {
        std::string s = api_str;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (s == "opengl")
            return RendererAPI::API::opengl;
        if (s == "vulkan")
            return RendererAPI::API::vulkan;

        HN_CORE_WARN("Unknown Renderer API '{}', using fallback", api_str);
        return fallback;
    }

    static RendererSettings::TextureFilter parse_texture_filter(
        const std::string& str,
        RendererSettings::TextureFilter fallback
    ) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (s == "nearest")     return RendererSettings::TextureFilter::nearest;
        if (s == "linear")      return RendererSettings::TextureFilter::linear;
        if (s == "anisotropic") return RendererSettings::TextureFilter::anisotropic;

        HN_CORE_WARN("Unknown TextureFilter '{}', using fallback", str);
        return fallback;
    }

    static std::string texture_filter_to_string(RendererSettings::TextureFilter tf) {
        switch (tf) {
        case RendererSettings::TextureFilter::nearest:     return "Nearest";
        case RendererSettings::TextureFilter::linear:      return "Linear";
        case RendererSettings::TextureFilter::anisotropic: return "Anisotropic";
        }
        return "Nearest";
    }

    static std::string cull_mode_to_string(CullMode mode) {
        switch (mode) {
        case CullMode::None: return "None";
        case CullMode::Front: return "Front";
        case CullMode::Back: return "Back";
        }
        return "None";
    }

    static CullMode string_to_cull_mode(const std::string& str) {
        std::string s = str;
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        if (s == "None") return CullMode::None;
        if (s == "Front") return CullMode::Front;
        if (s == "Back") return CullMode::Back;
        return CullMode::None;
    }

    bool Settings::load_from_file(const std::filesystem::path& filepath) {
        if (!std::filesystem::exists(filepath)) {
            HN_CORE_WARN("Settings file does not exist: {}", filepath.string());
            return false;
        }

        std::ifstream stream(filepath);
        if (!stream.is_open()) {
            HN_CORE_ERROR("Failed to open settings file: {}", filepath.string());
            return false;
        }

        std::stringstream ss;
        ss << stream.rdbuf();

        YAML::Node root;
        try {
            root = YAML::Load(ss.str());
        } catch (const YAML::ParserException& e) {
            HN_CORE_ERROR("Failed to parse settings YAML: {}", e.what());
            return false;
        }

        EngineSettings& s = get();

        // ---------------- Renderer ----------------
        if (auto renderer_node = root["Renderer"]) {
            // API
            if (auto api_node = renderer_node["API"]) {
                s.renderer.api = parse_renderer_api(
                    api_node.as<std::string>(),
                    s.renderer.api
                );
            }

            // ClearColor (reuse glm::vec4 support from scene_serializer.cpp)
            if (auto clear_node = renderer_node["ClearColor"]) {
                try {
                    s.renderer.clear_color = clear_node.as<glm::vec4>();
                } catch (const YAML::BadConversion& e) {
                    HN_CORE_WARN("Bad ClearColor in settings: {}", e.what());
                }
            }

            if (auto n = renderer_node["Wireframe"])
                s.renderer.wireframe = n.as<bool>(s.renderer.wireframe);
            if (auto n = renderer_node["DepthTest"])
                s.renderer.depth_test = n.as<bool>(s.renderer.depth_test);
            if (auto n = renderer_node["DepthWrite"])
                s.renderer.depth_write = n.as<bool>(s.renderer.depth_write);
            if (auto n = renderer_node["FaceCulling"])
                s.renderer.face_culling = n.as<bool>(s.renderer.face_culling);
            if (auto n = renderer_node["Blending"])
                s.renderer.blending = n.as<bool>(s.renderer.blending);
            if (auto n = renderer_node["VSync"])
                s.renderer.vsync = n.as<bool>(s.renderer.vsync);
            if (auto n = renderer_node["ShowPhysicsDebugDraw"])
                s.renderer.show_physics_debug_draw = n.as<bool>(s.renderer.show_physics_debug_draw);

            if (auto n = renderer_node["AnisotropicFilteringLevel"]) {
                try {
                    s.renderer.anisotropic_filtering_level =
                        n.as<float>(s.renderer.anisotropic_filtering_level);
                } catch (const YAML::BadConversion& e) {
                    HN_CORE_WARN("Bad AnisotropicFilteringLevel in settings: {}", e.what());
                }
            }

            if (auto n = renderer_node["TextureFilter"]) {
                s.renderer.texture_filter = parse_texture_filter(
                    n.as<std::string>(),
                    s.renderer.texture_filter
                );
            }

            if (auto n = renderer_node["CullMode"]) {
                s.renderer.cull_mode = string_to_cull_mode(n.as<std::string>());
            }
        }

        // ---------------- Physics ----------------
        if (auto physics_node = root["Physics"]) {
            if (auto n = physics_node["Enabled"])
                s.physics.enabled = n.as<bool>(s.physics.enabled);

            if (auto n = physics_node["Substeps"]) {
                try {
                    s.physics.substeps = n.as<int>(s.physics.substeps);
                } catch (const YAML::BadConversion& e) {
                    HN_CORE_WARN("Bad Physics.Substeps in settings: {}", e.what());
                }
            }
        }

        HN_CORE_INFO("Loaded settings from {}", filepath.string());
        return true;
    }

    bool Settings::save_to_file(const std::filesystem::path& filepath) {
        EngineSettings& s = get();

        YAML::Emitter out;
        out << YAML::BeginMap;

        // ---------------- Renderer ----------------
        out << YAML::Key << "Renderer" << YAML::Value;
        out << YAML::BeginMap;

        out << YAML::Key << "API"                     << YAML::Value << RendererAPI::to_string(s.renderer.api);
        out << YAML::Key << "ClearColor"              << YAML::Value << s.renderer.clear_color;
        out << YAML::Key << "Wireframe"               << YAML::Value << s.renderer.wireframe;
        out << YAML::Key << "DepthTest"               << YAML::Value << s.renderer.depth_test;
        out << YAML::Key << "DepthWrite"              << YAML::Value << s.renderer.depth_write;
        out << YAML::Key << "FaceCulling"             << YAML::Value << s.renderer.face_culling;
        out << YAML::Key << "Blending"                << YAML::Value << s.renderer.blending;
        out << YAML::Key << "VSync"                   << YAML::Value << s.renderer.vsync;
        out << YAML::Key << "ShowPhysicsDebugDraw"    << YAML::Value << s.renderer.show_physics_debug_draw;
        out << YAML::Key << "AnisotropicFilteringLevel"
            << YAML::Value << s.renderer.anisotropic_filtering_level;
        out << YAML::Key << "TextureFilter"
            << YAML::Value << texture_filter_to_string(s.renderer.texture_filter);
        out << YAML::Key << "CullMode"
            << YAML::Value << cull_mode_to_string(s.renderer.cull_mode);

        out << YAML::EndMap; // Renderer

        // ---------------- Physics ----------------
        out << YAML::Key << "Physics" << YAML::Value;
        out << YAML::BeginMap;

        out << YAML::Key << "Enabled"  << YAML::Value << s.physics.enabled;
        out << YAML::Key << "Substeps" << YAML::Value << s.physics.substeps;

        out << YAML::EndMap; // Physics

        out << YAML::EndMap; // root

        std::filesystem::create_directories(filepath.parent_path());

        std::ofstream stream(filepath);
        if (!stream.is_open()) {
            HN_CORE_ERROR("Failed to open settings file for writing: {}", filepath.string());
            return false;
        }

        stream << out.c_str();
        HN_CORE_INFO("Saved settings to {}", filepath.string());
        return true;
    }

    bool Settings::write_renderer_api_to_file(const std::filesystem::path& filepath) {
        // Load existing file if it exists
        YAML::Node root;

        if (std::filesystem::exists(filepath)) {
            std::ifstream stream(filepath);
            if (!stream.is_open()) {
                HN_CORE_ERROR("Failed to open settings file for updating Renderer.API: {}", filepath.string());
                return false;
            }

            std::stringstream ss;
            ss << stream.rdbuf();

            try {
                root = YAML::Load(ss.str());
            } catch (const YAML::ParserException& e) {
                HN_CORE_ERROR("Failed to parse settings YAML while updating Renderer.API: {}", e.what());
                return false;
            }
        } else {
            // If file doesn't exist yet, start from an empty map
            root = YAML::Node(YAML::NodeType::Map);
        }

        // Ensure "Renderer" node exists
        YAML::Node renderer_node = root["Renderer"];
        if (!renderer_node || !renderer_node.IsMap()) {
            renderer_node = YAML::Node(YAML::NodeType::Map);
        }

        // Write only the API value from current Settings
        const EngineSettings& s = get();
        renderer_node["API"] = RendererAPI::to_string(s.renderer.api);

        // Reattach the renderer node
        root["Renderer"] = renderer_node;

        // Write the updated YAML back to disk
        std::filesystem::create_directories(filepath.parent_path());

        std::ofstream out_stream(filepath);
        if (!out_stream.is_open()) {
            HN_CORE_ERROR("Failed to open settings file for writing Renderer.API: {}", filepath.string());
            return false;
        }

        out_stream << root;
        HN_CORE_INFO("Updated Renderer.API in settings file: {}", filepath.string());
        return true;
    }
}

