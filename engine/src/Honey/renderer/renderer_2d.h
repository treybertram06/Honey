#pragma once

#include "camera.h"
#include "editor_camera.h"
#include "texture.h"
#include "sub_texture_2d.h"
#include "Honey/scene/components.h"

namespace Honey {

    class Renderer2D {
    public:
        static void init();
        static void shutdown();

        static void begin_scene(const Camera& camera, const glm::mat4& transform);
        static void begin_scene(const EditorCamera& camera);
        static void begin_scene(const OrthographicCamera& camera);
        static void end_scene();

        // Position-based overloads
        static void draw_quad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
        static void draw_quad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);
        
        static void draw_quad(const glm::vec2& position, const glm::vec2& size,
                             const Ref<Texture2D>& texture = nullptr,
                             const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                             float tiling_factor = 1.0f);
        static void draw_quad(const glm::vec3& position, const glm::vec2& size,
                             const Ref<Texture2D>& texture = nullptr,
                             const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                             float tiling_factor = 1.0f);

        // Rotated variants
        static void draw_rotated_quad(const glm::vec2& position, const glm::vec2& size, float rotation, const glm::vec4& color);
        static void draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
        
        static void draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                                     const Ref<Texture2D>& texture = nullptr,
                                     const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                                     float tiling_factor = 1.0f);

        // SubTexture variants
        static void draw_quad(const glm::vec3& position, const glm::vec2& size,
                             const Ref<SubTexture2D>& sub_texture,
                             const glm::vec4& color = glm::vec4(1.0f),
                             float tiling_factor = 1.0f);
        
        static void draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                                     const Ref<SubTexture2D>& sub_texture,
                                     const glm::vec4& color = glm::vec4(1.0f),
                                     float tiling_factor = 1.0f);

        // Transform matrix variants
        static void draw_quad(const glm::mat4& transform, const glm::vec4& color);
        static void draw_quad(const glm::mat4& transform,
                             const Ref<Texture2D>& texture = nullptr,
                             const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                             float tiling_factor = 1.0f);

        static void draw_sprite(const glm::mat4& transform, SpriteRendererComponent& src, int entity_id);

        // Statistics
        struct Statistics {
            uint32_t draw_calls = 0;
            uint32_t quad_count = 0;

            uint32_t get_total_vertex_count() { return quad_count * 4; }
            uint32_t get_total_index_count() { return quad_count * 6; }
        };
        static Statistics get_stats();
        static void reset_stats();

    private:
        // Core implementation function
        static void submit_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
                               const Ref<Texture2D>& texture, const Ref<SubTexture2D>& sub_texture,
                               const glm::vec4& color, float tiling_factor, int entity_id = -1);
        
        // Transform decomposition helper
        static void decompose_transform(const glm::mat4& transform, glm::vec3& position, 
                                       glm::vec2& scale, float& rotation);
    };
}
