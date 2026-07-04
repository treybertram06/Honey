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

    void ensure_instance_buffer_capacity(uint32_t required_instances) {
        HN_CORE_ASSERT(g_renderer3d_data, "Renderer3D: g_renderer3d_data null");

        if (required_instances == 0)
            return;

        if (!g_renderer3d_data->instance_vb || g_renderer3d_data->instance_vb_capacity < required_instances) {
            uint32_t new_cap = std::max(64u, g_renderer3d_data->instance_vb_capacity);
            while (new_cap < required_instances)
                new_cap *= 2;

            g_renderer3d_data->instance_vb_capacity = new_cap;

            const uint32_t bytes = g_renderer3d_data->instance_vb_capacity * sizeof(InstanceData);
            g_renderer3d_data->instance_vb = VertexBuffer::create(bytes);
            g_renderer3d_data->instance_vb->set_layout({
                { ShaderDataType::Float4, "a_iModel0", false, true },
                { ShaderDataType::Float4, "a_iModel1", false, true },
                { ShaderDataType::Float4, "a_iModel2", false, true },
                { ShaderDataType::Float4, "a_iModel3", false, true },
                { ShaderDataType::Int, "a_iEntityID", false, true },
            });
        }
    }

    void flush_batches_vulkan(const PipelineFactory& get_pipeline) {
        HN_PROFILE_FUNCTION();

        if (!g_renderer3d_data->vk_context_cache) {
            auto* base = Application::get().get_window().get_context();
            g_renderer3d_data->vk_context_cache = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(g_renderer3d_data->vk_context_cache, "Renderer3D Vulkan path expected VulkanContext");
        }
        auto* vk_ctx = g_renderer3d_data->vk_context_cache;

        CameraUBO saved_camera = VulkanRendererAPI::get_globals_state().cameraUBO;

        void* rp_native = nullptr;
        if (auto target = Renderer::get_render_target()) {
            auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
            HN_CORE_ASSERT(vk_fb, "Renderer3D: current render target is not a VulkanFramebuffer");
            rp_native = vk_fb->get_render_pass();
        } else {
            rp_native = vk_ctx->get_render_pass();
        }
        HN_CORE_ASSERT(rp_native, "Renderer3D: rpNative is null");

        uint32_t total_instances = 0;
        for (auto& [key, batch] : g_renderer3d_data->batches)
            total_instances += (uint32_t)batch.transforms.size();

        if (total_instances == 0)
            return;

        std::vector<InstanceData> packed;
        packed.reserve(total_instances);

        struct OrderedBatch {
            const BatchKey* key;
            const BatchValue* batch;
            uint32_t start_index;
        };

        std::vector<OrderedBatch> ordered_batches;
        ordered_batches.reserve(g_renderer3d_data->batches.size());

        for (auto& [key, batch] : g_renderer3d_data->batches) {
            if (batch.transforms.empty())
                continue;

            const uint32_t start_index = (uint32_t)packed.size();
            for (size_t i = 0; i < batch.transforms.size(); ++i) {
                InstanceData inst{};
                inst.model = batch.transforms[i];
                inst.entity_id = (i < batch.entity_ids.size()) ? batch.entity_ids[i] : -1;
                packed.push_back(inst);
            }

            ordered_batches.push_back(OrderedBatch{&key, &batch, start_index});
        }

        HN_CORE_ASSERT(packed.size() == total_instances,
                       "Renderer3D: packed instance count mismatch (packed={}, expected={})",
                       packed.size(), total_instances);

        ensure_instance_buffer_capacity((uint32_t)packed.size());
        g_renderer3d_data->instance_vb->set_data(packed.data(), (uint32_t)(packed.size() * sizeof(InstanceData)));

        struct MaterialPC {
            int32_t material_index;
            int32_t _pad[3];
        };
        static_assert(sizeof(MaterialPC) <= 128, "MaterialPC too large");

        std::vector<GPUMaterial> all_materials;
        all_materials.reserve(ordered_batches.size());

        for (uint32_t i = 0; i < (uint32_t)ordered_batches.size(); ++i) {
            auto* mat = ordered_batches[i].batch->material.get();
            all_materials.push_back(build_gpu_material(mat));
        }

        if (!ordered_batches.empty()) {
            const auto* first_mat = ordered_batches[0].batch->material.get();
            const bool blend = first_mat && first_mat->get_alpha_mode() == Material::AlphaMode::Blend;
            const bool cull_none = first_mat && first_mat->get_double_sided();
            Ref<Pipeline> first_pipe = get_pipeline(rp_native, blend, cull_none);
            RenderCommand::bind_pipeline(first_pipe);
            g_renderer3d_data->stats.pipeline_binds++;
        }

        VulkanRendererAPI::submit_camera(saved_camera);
        //VulkanRendererAPI::submit_bound_textures(tex_array, tex_slot_count); // TEMP: This will break forward pipeline
        VulkanRendererAPI::submit_materials(all_materials, 0);
        VulkanRendererAPI::flush_globals();

        Ref<Pipeline> current_pipe;
        for (uint32_t i = 0; i < (uint32_t)ordered_batches.size(); ++i) {
            const auto* mat = ordered_batches[i].batch->material.get();
            const bool blend = mat && mat->get_alpha_mode() == Material::AlphaMode::Blend;
            const bool cull_none = mat && mat->get_double_sided();
            Ref<Pipeline> pipe = get_pipeline(rp_native, blend, cull_none);

            if (pipe != current_pipe) {
                RenderCommand::bind_pipeline(pipe);
                g_renderer3d_data->stats.pipeline_binds++;
                current_pipe = pipe;
            }

            const uint32_t byte_offset = ordered_batches[i].start_index * (uint32_t)sizeof(InstanceData);
            HN_CORE_ASSERT((byte_offset % 4u) == 0u, "Renderer3D: instance byte offset must be 4-byte aligned");

            MaterialPC pc{(int32_t)i};
            VulkanRendererAPI::submit_push_constants(&pc, sizeof(MaterialPC));

            VulkanRendererAPI::submit_instanced_draw(
                ordered_batches[i].batch->va,
                g_renderer3d_data->instance_vb,
                0,
                (uint32_t)ordered_batches[i].batch->transforms.size(),
                byte_offset);

            g_renderer3d_data->stats.draw_calls++;
        }

        // Populate classic shadow draw list
        if (g_renderer3d_data->geometry_path == GeometryPath::Classic) {
            g_renderer3d_data->shadow_draw_list.clear();
            for (auto& ob : ordered_batches) {
                const auto& vbs = ob.batch->va->get_vertex_buffers();
                const auto& ib = ob.batch->va->get_index_buffer();
                if (vbs.empty() || !ib) continue;

                ClassicShadowDrawEntry entry{};
                entry.vertex_buffer = reinterpret_cast<VkBuffer>(vbs[0]->get_native_buffer());
                entry.index_buffer = reinterpret_cast<VkBuffer>(ib->get_native_buffer());
                entry.index_count = ib->get_count();
                entry.draw_data_base = ob.start_index;
                entry.draw_count = (uint32_t)ob.batch->transforms.size();

                g_renderer3d_data->classic_shadow_draw_list.push_back(entry);
            }
        }
    }
}
