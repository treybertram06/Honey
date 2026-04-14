#pragma once

#include "Honey/core/base.h"
#include "texture.h"

#include <glm/glm.hpp>

namespace Honey {

    class Material {
    public:
        enum class AlphaMode : uint8_t {
            Opaque = 0,
            Mask = 1,
            Blend = 2
        };

        struct TextureTransform {
            glm::vec2 offset{0.0f, 0.0f};
            glm::vec2 scale{1.0f, 1.0f};
            float rotation = 0.0f;
            bool has_transform = false;
        };

        struct TextureSlot {
            Ref<Texture2D> texture{};
            int tex_coord = 0;
            int gltf_texture_index = -1;
            int gltf_image_source = -1;
            TextureTransform transform{};
        };

        struct KHRMaterials {
            struct Clearcoat {
                bool enabled = false;
                float factor = 0.0f;
                TextureSlot texture{};
                float roughness_factor = 0.0f;
                TextureSlot roughness_texture{};
                TextureSlot normal_texture{};
                float normal_scale = 1.0f;
            } clearcoat;

            struct Sheen {
                bool enabled = false;
                glm::vec3 color_factor{0.0f, 0.0f, 0.0f};
                TextureSlot color_texture{};
                float roughness_factor = 0.0f;
                TextureSlot roughness_texture{};
            } sheen;

            struct Specular {
                bool enabled = false;
                float specular_factor = 1.0f;
                TextureSlot specular_texture{};
                glm::vec3 specular_color_factor{1.0f, 1.0f, 1.0f};
                TextureSlot specular_color_texture{};
            } specular;

            struct Transmission {
                bool enabled = false;
                float factor = 0.0f;
                TextureSlot texture{};
            } transmission;

            struct Volume {
                bool enabled = false;
                float thickness_factor = 0.0f;
                TextureSlot thickness_texture{};
                float attenuation_distance = 0.0f;
                glm::vec3 attenuation_color{1.0f, 1.0f, 1.0f};
            } volume;

            struct Ior {
                bool enabled = false;
                float ior = 1.5f;
            } ior;

            struct Iridescence {
                bool enabled = false;
                float factor = 0.0f;
                TextureSlot texture{};
                float ior = 1.3f;
                float thickness_min = 100.0f;
                float thickness_max = 400.0f;
                TextureSlot thickness_texture{};
            } iridescence;

            struct Anisotropy {
                bool enabled = false;
                float strength = 0.0f;
                float rotation = 0.0f;
                TextureSlot texture{};
            } anisotropy;

            struct EmissiveStrength {
                bool enabled = false;
                float strength = 1.0f;
            } emissive_strength;

            struct Unlit {
                bool enabled = false;
            } unlit;

            struct PbrSpecularGlossiness {
                bool enabled = false;
                glm::vec4 diffuse_factor{1.0f, 1.0f, 1.0f, 1.0f};
                TextureSlot diffuse_texture{};
                glm::vec3 specular_factor{1.0f, 1.0f, 1.0f};
                float glossiness_factor = 1.0f;
                TextureSlot specular_glossiness_texture{};
            } pbr_specular_glossiness;
        };

        struct PBR {
            glm::vec4 base_color_factor{1.0f, 1.0f, 1.0f, 1.0f};
            TextureSlot base_color_texture{};

            float metallic_factor = 1.0f;
            float roughness_factor = 1.0f;
            TextureSlot metallic_roughness_texture{};

            TextureSlot normal_texture{};
            float normal_scale = 1.0f;

            TextureSlot occlusion_texture{};
            float occlusion_strength = 1.0f;

            glm::vec3 emissive_factor{0.0f, 0.0f, 0.0f};
            TextureSlot emissive_texture{};

            AlphaMode alpha_mode = AlphaMode::Opaque;
            float alpha_cutoff = 0.5f;
            bool double_sided = false;

            KHRMaterials extensions{};
        };

    public:
        Material() = default;
        ~Material() = default;

        static Ref<Material> create() { return CreateRef<Material>(); }

        const PBR& pbr() const { return m_pbr; }
        PBR& pbr() { return m_pbr; }

        void set_base_color_factor(const glm::vec4& factor) { m_pbr.base_color_factor = factor; }
        const glm::vec4& get_base_color_factor() const { return m_pbr.base_color_factor; }

        void set_base_color_texture(const Ref<Texture2D>& texture) { m_pbr.base_color_texture.texture = texture; }
        const Ref<Texture2D>& get_base_color_texture() const { return m_pbr.base_color_texture.texture; }

        void set_metallic_factor(float factor) { m_pbr.metallic_factor = factor; }
        float get_metallic_factor() const { return m_pbr.metallic_factor; }

        void set_roughness_factor(float factor) { m_pbr.roughness_factor = factor; }
        float get_roughness_factor() const { return m_pbr.roughness_factor; }

        void set_metallic_roughness_texture(const Ref<Texture2D>& texture) {
            m_pbr.metallic_roughness_texture.texture = texture;
        }
        const Ref<Texture2D>& get_metallic_roughness_texture() const {
            return m_pbr.metallic_roughness_texture.texture;
        }

        void set_normal_texture(const Ref<Texture2D>& texture) { m_pbr.normal_texture.texture = texture; }
        const Ref<Texture2D>& get_normal_texture() const { return m_pbr.normal_texture.texture; }

        void set_normal_scale(float scale) { m_pbr.normal_scale = scale; }
        float get_normal_scale() const { return m_pbr.normal_scale; }

        void set_occlusion_texture(const Ref<Texture2D>& texture) { m_pbr.occlusion_texture.texture = texture; }
        const Ref<Texture2D>& get_occlusion_texture() const { return m_pbr.occlusion_texture.texture; }

        void set_occlusion_strength(float strength) { m_pbr.occlusion_strength = strength; }
        float get_occlusion_strength() const { return m_pbr.occlusion_strength; }

        void set_emissive_factor(const glm::vec3& factor) { m_pbr.emissive_factor = factor; }
        const glm::vec3& get_emissive_factor() const { return m_pbr.emissive_factor; }

        void set_emissive_texture(const Ref<Texture2D>& texture) { m_pbr.emissive_texture.texture = texture; }
        const Ref<Texture2D>& get_emissive_texture() const { return m_pbr.emissive_texture.texture; }

        void set_alpha_mode(AlphaMode mode) { m_pbr.alpha_mode = mode; }
        AlphaMode get_alpha_mode() const { return m_pbr.alpha_mode; }

        void set_alpha_cutoff(float cutoff) { m_pbr.alpha_cutoff = cutoff; }
        float get_alpha_cutoff() const { return m_pbr.alpha_cutoff; }

        void set_double_sided(bool value) { m_pbr.double_sided = value; }
        bool get_double_sided() const { return m_pbr.double_sided; }

        bool has_base_color_texture() const { return (bool)m_pbr.base_color_texture.texture; }
        bool has_metallic_roughness_texture() const { return (bool)m_pbr.metallic_roughness_texture.texture; }
        bool has_normal_texture() const { return (bool)m_pbr.normal_texture.texture; }
        bool has_occlusion_texture() const { return (bool)m_pbr.occlusion_texture.texture; }
        bool has_emissive_texture() const { return (bool)m_pbr.emissive_texture.texture; }

    private:
        PBR m_pbr{};
    };

} // namespace Honey
