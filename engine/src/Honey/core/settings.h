#pragma once
#include <glm/glm.hpp>
#include "Honey/renderer/renderer_api.h"

namespace Honey {

    struct RendererSettings {

        RendererAPI::API api = RendererAPI::API::opengl;

        glm::vec4 clear_color = { 0.1f, 0.1f, 0.1f, 1.0f };
        bool wireframe = false;
        bool depth_test = false;
        bool depth_write = false;
        bool face_culling = true;
        bool blending = true;
        bool vsync = true;
        bool show_physics_debug_draw = false;


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

    EngineSettings& get_settings();
}