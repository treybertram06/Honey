#pragma once
#include <glm/glm.hpp>
#include "Honey/renderer/renderer_api.h"

namespace Honey {

    struct RendererSettings {

        enum class TextureFilter {
            nearest = 0,
            linear,
            anisotropic,
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

        TextureFilter texture_filter = TextureFilter::nearest;


    };

    struct PhysicsSettings {
        bool enabled = true;
        int substeps = 6;
        //bool single_step = false;
    };

    struct EngineSettings {
        RendererSettings renderer;
        PhysicsSettings physics;
    };

    class Settings {
    public:
        static EngineSettings& get() {
            static EngineSettings s_instance;
            return s_instance;
        }
    };

}