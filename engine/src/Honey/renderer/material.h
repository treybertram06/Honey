#pragma once

#include "Honey/core/base.h"
#include "texture.h"

#include <glm/glm.hpp>

namespace Honey {

    class Material {
    public:
        struct PBR {
            // glTF: pbrMetallicRoughness.baseColorFactor
            glm::vec4 base_color_factor{ 1.0f, 1.0f, 1.0f, 1.0f };

            // glTF: pbrMetallicRoughness.baseColorTexture (optional)
            Ref<Texture2D> base_color_texture{};
        };

    public:
        Material() = default;
        ~Material() = default;

        static Ref<Material> create() { return CreateRef<Material>(); }

        // --- PBR (minimal v1) ---
        const PBR& pbr() const { return m_pbr; }
        PBR& pbr() { return m_pbr; }

        void set_base_color_factor(const glm::vec4& factor) { m_pbr.base_color_factor = factor; }
        const glm::vec4& get_base_color_factor() const { return m_pbr.base_color_factor; }

        void set_base_color_texture(const Ref<Texture2D>& texture) { m_pbr.base_color_texture = texture; }
        const Ref<Texture2D>& get_base_color_texture() const { return m_pbr.base_color_texture; }

        bool has_base_color_texture() const { return (bool)m_pbr.base_color_texture; }

    private:
        PBR m_pbr{};
    };

} // namespace Honey