#pragma once

#include "camera.h"
#include "texture.h"

namespace Honey {

    class Renderer2D {
    public:

        static void init();
        static void shutdown();

        static void begin_scene(const OrthographicCamera& camera);
        static void end_scene();

        // primitives
        static void draw_quad(const glm::vec3& position, const glm::vec2& size,
                             const Ref<Texture2D>& texture = nullptr,
                             const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                             float tiling_multiplier = 1.0f);

        static void draw_quad(const glm::vec2& position, const glm::vec2& size,
                             const Ref<Texture2D>& texture = nullptr,
                             const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
                             float tiling_multiplier = 1.0f);

        static void draw_quad(const glm::vec2& position, const glm::vec2& size, const glm::vec4& color);
        static void draw_quad(const glm::vec3& position, const glm::vec2& size, const glm::vec4& color);


    	static void draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation,
							 const Ref<Texture2D>& texture = nullptr,
							 const glm::vec4& color = {1.0f, 1.0f, 1.0f, 1.0f},
							 float tiling_multiplier = 1.0f);

    	static void draw_rotated_quad(const glm::vec3& position, const glm::vec2& size, float rotation, const glm::vec4& color);
    };
}