#pragma once
#include <glm/glm.hpp>
#include <cstddef>
#include "tiled_lighting_constants.h"

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
        PointLight point_lights[k_max_point_lights];

        void set_point_light_count(int count) { directional_light.point_light_count = count; }
    };

    // Per-frame tiled lighting data — uploaded as an SSBO to set=0 binding=5.
    // sorted_light_indices[i] is the original LightsUBO index of the i-th front-to-back light.
    // tile_light_masks[tile] is a bitmask where bit N means point_lights[N] affects that tile.
    struct TiledLightingData {
        uint32_t tile_count_x = 0;
        uint32_t tile_count_y = 0;
        uint32_t light_count  = 0;
        uint32_t _pad         = 0;
        uint32_t sorted_light_indices[k_max_point_lights]{};
        uint32_t tile_light_masks[k_max_tiles]{};
    };

    struct CameraUBO {
        glm::mat4 view_proj{};
        glm::vec3 position{};
        float _pad0 = 0;
        glm::mat4 inv_view_proj{};  // inverse VP for deferred lighting position reconstruction
    };

    struct alignas(16) GPUMaterial {
        alignas(16) glm::vec4 base_color{1.0f, 1.0f, 1.0f, 1.0f};
        alignas(16) glm::vec4 emissive_factor{0.0f, 0.0f, 0.0f, 1.0f};

        float metallic = 1.0f;
        float roughness = 1.0f;
        float normal_scale = 1.0f;
        float occlusion_strength = 1.0f;

        float alpha_cutoff = 0.5f;
        int32_t alpha_mode = 0; // 0=opaque, 1=mask, 2=blend
        int32_t double_sided = 0;
        int32_t unlit = 0;

        int32_t base_color_tex_id = -1;
        int32_t metallic_roughness_tex_id = -1;
        int32_t normal_tex_id = -1;
        int32_t occlusion_tex_id = -1;
        int32_t emissive_tex_id = -1;

        int32_t base_color_uv_set = 0;
        int32_t metallic_roughness_uv_set = 0;
        int32_t normal_uv_set = 0;
        int32_t occlusion_uv_set = 0;
        int32_t emissive_uv_set = 0;

        alignas(16) glm::vec4 base_color_uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};
        alignas(16) glm::vec4 metallic_roughness_uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};
        alignas(16) glm::vec4 normal_uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};
        alignas(16) glm::vec4 occlusion_uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};
        alignas(16) glm::vec4 emissive_uv_scale_offset{1.0f, 1.0f, 0.0f, 0.0f};

        float base_color_uv_rotation = 0.0f;
        float metallic_roughness_uv_rotation = 0.0f;
        float normal_uv_rotation = 0.0f;
        float occlusion_uv_rotation = 0.0f;
        float emissive_uv_rotation = 0.0f;

        float _pad0 = 0.0f;
        float _pad1 = 0.0f;
        float _pad2 = 0.0f;
    };
    static_assert(sizeof(GPUMaterial) == 224, "GPUMaterial layout mismatch");
    static_assert(offsetof(GPUMaterial, base_color_uv_scale_offset) == 112, "GPUMaterial std430 offset mismatch");

    struct GPUDrawData {
        glm::mat4   model;
        uint32_t    meshlet_offset; // byte offset into global meshlet buffer
        uint32_t    meshlet_count;
        int32_t     material_index;
        int32_t     entity_id;
    };

} // namespace Honey
