#pragma once
#include "hnpch.h"
#include "Honey/renderer/texture.h"
#include "glm/glm.hpp"

namespace Honey {

    class Sprite {
    public:

        Sprite() = default;
        Sprite(const Ref<Texture2D>& texture, glm::ivec2 pixel_min, glm::ivec2 pixel_size,
            float pixels_per_unit = 100.0f, glm::vec2 pivot = {0.5f, 0.5f})
            : m_texture(texture), m_pixel_min(pixel_min), m_pixel_size(pixel_size),
              m_pixels_per_unit(pixels_per_unit), m_pivot(pivot)
        {
            validate();
        }


        static Ref<Sprite> create_from_texture(const Ref<Texture2D>& texture, float pixels_per_unit = 100.0f, glm::vec2 pivot = {0.5f, 0.5f});

        glm::vec2 get_world_size() const {
            return {
                m_pixel_size.x / m_pixels_per_unit,
                m_pixel_size.y / m_pixels_per_unit
            };
        }

        void get_uvs(glm::vec2& out_min, glm::vec2& out_max) const {
            const glm::vec2 tex_size = {
                (float)m_texture->get_width(),
                (float)m_texture->get_height()
            };

            out_min = glm::vec2(m_pixel_min) / tex_size;
            out_max = (glm::vec2(m_pixel_min) + glm::vec2(m_pixel_size)) / tex_size;
        }

        glm::vec2 get_pivot_offset() const {
            return (glm::vec2(0.5f, 0.5f) - m_pivot) * get_world_size();
        }

        Ref<Texture2D> get_texture() const { return m_texture; }
        void set_texture(const Ref<Texture2D>& texture) { m_texture = texture; recalc_size(); }

        float get_pixels_per_unit() const { return m_pixels_per_unit; }
        void  set_pixels_per_unit(float value) {
            if (value > 0.0f) {
                m_pixels_per_unit = value;
            }
        }

        glm::vec2 get_pivot() const { return m_pivot; }
        void      set_pivot(const glm::vec2& pivot) { m_pivot = pivot; }

        glm::ivec2 get_pixel_min() const { return m_pixel_min; }
        glm::ivec2 get_pixel_size() const { return m_pixel_size; }

        void recalc_size() {
            m_pixel_size = {m_texture->get_width(), m_texture->get_height()};
        }

    private:
        Ref<Texture2D> m_texture;

        // Pixel-space rectangle inside the texture
        glm::ivec2 m_pixel_min {0, 0};   // bottom-left
        glm::ivec2 m_pixel_size {0, 0};  // width / height in pixels

        float m_pixels_per_unit = 100.0f;
        glm::vec2 m_pivot = {0.5f, 0.5f};

        void validate() const;
    };

} // namespace Honey