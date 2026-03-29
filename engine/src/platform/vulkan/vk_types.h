#pragma once

namespace Honey {

    struct LightsUBO {
        struct DirectionalLight {
            glm::vec3 direction{};
            float intensity = 0.0f;
            glm::vec3 color{};
            int point_light_count = 0;
        };

        struct PointLight {
            glm::vec3 position{};
            float intensity = 0.0f;
            glm::vec3 color{};
            float range = 0.0f;
        };

        static_assert(sizeof(DirectionalLight) == 32, "DirectionalLight layout mismatch");
        static_assert(sizeof(PointLight) == 32, "PointLight layout mismatch");

        DirectionalLight directional_light;
        PointLight point_lights[32];

        void set_point_light_count(int count) { directional_light.point_light_count = count; }
    };

    struct CameraUBO {
        glm::mat4 view_proj{};
        glm::vec3 position{};
        float _pad0 = 0;
    };

}