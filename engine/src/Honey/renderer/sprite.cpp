#include "hnpch.h"
#include "sprite.h"

namespace Honey {

    Ref<Sprite> Sprite::create_from_texture(const Ref<Texture2D>& texture, float pixels_per_unit, glm::vec2 pivot)  {
        if (!texture) {
            HN_CORE_ERROR("Sprite::CreateFromTexture called with null texture");
            return nullptr;
        }

        return CreateRef<Sprite>(texture, glm::ivec2{0, 0}, glm::ivec2{ (int)texture->get_width(), (int)texture->get_height()},
            pixels_per_unit, pivot );
    }

    void Sprite::validate() const  {
        if (!m_texture) {
            HN_CORE_ERROR("Sprite created with null texture");
            return;
        }

        if (m_pixel_size.x <= 0 || m_pixel_size.y <= 0) {
            HN_CORE_ERROR("Sprite created with invalid pixel size ({}, {})",
                          m_pixel_size.x, m_pixel_size.y);
        }

        if (m_pixels_per_unit <= 0.0f) {
            HN_CORE_ERROR("Sprite created with invalid pixels_per_unit ({})",
                          m_pixels_per_unit);
        }
    }

}
