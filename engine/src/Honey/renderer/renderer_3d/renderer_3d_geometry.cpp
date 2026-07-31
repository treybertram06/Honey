#include "hnpch.h"
#include "renderer_3d_internal.h"

#include "Honey/core/engine.h"
#include "Honey/renderer/render_command.h"
#include "Honey/renderer/renderer.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_texture.h"

namespace Honey::Renderer3DInternal {

    namespace {
        glm::vec4 slot_scale_offset(const Material::TextureSlot& slot) {
            return glm::vec4(slot.transform.scale.x, slot.transform.scale.y, slot.transform.offset.x, slot.transform.offset.y);
        }

        int32_t register_material_texture(Texture2D* tex) {
            if (!tex) return -1;
            auto* vk = dynamic_cast<VulkanTexture2D*>(tex);
            if (!vk) return -1;
            return (int32_t)vk->get_bindless_index();
        }
    }

    GPUMaterial build_gpu_material(const Material* mat) {
        GPUMaterial gpu{};
        if (!mat) {
            gpu.base_color_tex_id = register_material_texture(g_renderer3d_data->white_texture.get());
            return gpu;
        }

        const auto& pbr = mat->pbr();
        gpu.base_color = pbr.base_color_factor;
        gpu.emissive_factor = glm::vec4(pbr.emissive_factor * pbr.extensions.emissive_strength.strength, 1.0f);
        gpu.metallic = pbr.metallic_factor;
        gpu.roughness = pbr.roughness_factor;
        gpu.normal_scale = pbr.normal_scale;
        gpu.occlusion_strength = pbr.occlusion_strength;
        gpu.alpha_cutoff = pbr.alpha_cutoff;
        gpu.alpha_mode = (int32_t)pbr.alpha_mode;
        gpu.double_sided = pbr.double_sided ? 1 : 0;
        gpu.unlit = pbr.extensions.unlit.enabled ? 1 : 0;

        Texture2D* base_tex = pbr.base_color_texture.texture ? pbr.base_color_texture.texture.get() : g_renderer3d_data->white_texture.get();
        gpu.base_color_tex_id = register_material_texture(base_tex);
        gpu.metallic_roughness_tex_id = register_material_texture(pbr.metallic_roughness_texture.texture.get());
        gpu.normal_tex_id = register_material_texture(pbr.normal_texture.texture.get());
        gpu.occlusion_tex_id = register_material_texture(pbr.occlusion_texture.texture.get());
        gpu.emissive_tex_id = register_material_texture(pbr.emissive_texture.texture.get());

        gpu.base_color_uv_set = pbr.base_color_texture.tex_coord;
        gpu.metallic_roughness_uv_set = pbr.metallic_roughness_texture.tex_coord;
        gpu.normal_uv_set = pbr.normal_texture.tex_coord;
        gpu.occlusion_uv_set = pbr.occlusion_texture.tex_coord;
        gpu.emissive_uv_set = pbr.emissive_texture.tex_coord;

        gpu.base_color_uv_scale_offset = slot_scale_offset(pbr.base_color_texture);
        gpu.metallic_roughness_uv_scale_offset = slot_scale_offset(pbr.metallic_roughness_texture);
        gpu.normal_uv_scale_offset = slot_scale_offset(pbr.normal_texture);
        gpu.occlusion_uv_scale_offset = slot_scale_offset(pbr.occlusion_texture);
        gpu.emissive_uv_scale_offset = slot_scale_offset(pbr.emissive_texture);

        gpu.base_color_uv_rotation = pbr.base_color_texture.transform.rotation;
        gpu.metallic_roughness_uv_rotation = pbr.metallic_roughness_texture.transform.rotation;
        gpu.normal_uv_rotation = pbr.normal_texture.transform.rotation;
        gpu.occlusion_uv_rotation = pbr.occlusion_texture.transform.rotation;
        gpu.emissive_uv_rotation = pbr.emissive_texture.transform.rotation;
        return gpu;
    }
}
