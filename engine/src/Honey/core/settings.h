#pragma once
#include <glm/glm.hpp>

#include "Honey/renderer/mesh.h"
#include "Honey/renderer/renderer_api.h"
#include "Honey/renderer/pipeline_spec.h"

#include <filesystem>

namespace YAML { class Node; } // Keep yaml-cpp out of this widely included header

namespace Honey {

    enum class SettingOwner { user, project };
    enum class WindowMode { windowed = 0, maximized, fullscreen };

    struct WindowSettings {
        std::string title = "Honey Engine";
        uint32_t width = 1280;
        uint32_t height = 720;
        int pos_x = -1;
        int pos_y = -1;
        WindowMode mode = WindowMode::windowed;
    };

    struct RendererSettings {

        struct BloomSettings { // I think this hierarchical approach to organizing the settings will be beneficial
            float threshold = 1.0f;
            float soft_knee = 0.5f;
            float strength = 1.0;

            bool operator==(const BloomSettings&) const = default;
        };

        struct AntiAliasingSettings {
            enum class AAType { none = 0, fxaa, taa, };
            enum class FxaaDebugView { off = 0, edge_mask, orientation, edge_side, };
            float subpix = 0.75f;
            float edge_threshold = 0.166f;
            float edge_threshold_min = 0.0833f;
            AAType type = AAType::fxaa;
            FxaaDebugView debug_view = FxaaDebugView::off; // Not serialized; debug views shouldn't survive a restart

            bool operator==(const AntiAliasingSettings&) const = default;
        };

        enum class TextureFilter {
            nearest = 0,
            linear,
            anisotropic,
        };

        enum class RendererType {
            forward = 0,
            deferred,
            pathtracing,
            lite,
        };

        RendererAPI::API api = RendererAPI::API::vulkan;

        glm::vec4 clear_color = { 0.1f, 0.1f, 0.1f, 1.0f };
        bool wireframe = false;
        bool depth_test = false;
        bool depth_write = false;
        bool face_culling = true;
        bool blending = true;
        bool vsync = true;
        bool show_physics_debug_draw = false;
        float anisotropic_filtering_level = 16.0f; // This overrides what the actual maximum value is, but I don't care.
        CullMode cull_mode = CullMode::Back;

        GeometryPath geometry_path = GeometryPath::Meshlet;
        bool enable_parallel_mesh_submission = false;

        RendererType renderer_type = RendererType::forward;

        TextureFilter texture_filter = TextureFilter::nearest;

        float dir_shadow_distance = 50.0f;
        float editor_camera_exposure = 1.0f;
        float ibl_intensity = 1.0f;

        BloomSettings bloom;
        AntiAliasingSettings anti_aliasing;

    };

    struct PhysicsSettings {
        bool enabled = true;
        int substeps = 6;
        bool show_jolt_debug_draw = false;
    };

    struct EditorSettings {
        float gizmo_icon_size = 32.0f;
    };

    struct EngineSettings {
        RendererSettings renderer;
        PhysicsSettings physics;
        WindowSettings window;
        EditorSettings editor;
    };

    class Settings {
    public:
        static EngineSettings& get() {
            static EngineSettings s_instance;
            return s_instance;
        }

        // Ownership of every persisted key lives in schema() in settings.cpp: each key is either per user or per
        // project, never both, and nothing falls back across files.

        // User layer: user_data_dir()/settings.yaml. Only user-owned keys are read or written.
        static bool load_user(const std::filesystem::path& path);
        static bool save_user(const std::filesystem::path& path);

        // Project layer: the "Settings:" block of the open .honeyproj. Only project-owned keys.
        static void load_project(const YAML::Node& settings_block); // null/missing node = engine defaults
        static void reset_project();                                // project keys back to engine defaults
        static bool save_project(const std::filesystem::path& honeyproj);
        static bool project_dirty();                                // project keys changed since load/reset/save

        // Writes only Renderer.API (the API switch needs a restart; don't persist other pending tweaks with it).
        static bool write_renderer_api_to_file(const std::filesystem::path& path);
    };

}