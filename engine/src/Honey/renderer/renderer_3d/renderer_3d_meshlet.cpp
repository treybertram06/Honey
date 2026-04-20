#include "hnpch.h"
#include "renderer_3d_internal.h"

#include "Honey/core/engine.h"
#include "Honey/core/settings.h"
#include "Honey/renderer/render_command.h"
#include "Honey/renderer/renderer.h"
#include "platform/vulkan/vk_framebuffer.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey::Renderer3DInternal {

    Ref<Pipeline> get_or_create_meshlet_pipeline(void* rp_native, void* extra_layout, bool blend, bool cull_none) {
        HN_CORE_ASSERT(rp_native, "get_or_create_meshlet_pipeline: rp_native is null");
        PipelineVariantKey key{rp_native, (uint8_t)(blend ? 1 : 0), (uint8_t)(cull_none ? 1 : 0)};

        auto it = g_renderer3d_data->vk_meshlet_pipelines.find(key);
        if (it != g_renderer3d_data->vk_meshlet_pipelines.end())
            return it->second;

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_Meshlet.glsl");
        if (!spec.perColorAttachmentBlend.empty())
            spec.perColorAttachmentBlend[0].enabled = blend;
        if (cull_none)
            spec.cullMode = CullMode::None;

        auto pipeline = Pipeline::create(spec, rp_native, extra_layout);
        g_renderer3d_data->vk_meshlet_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    Ref<Pipeline> get_or_create_meshlet_gbuffer_pipeline(void* rp_native, void* extra_layout, bool cull_none) {
        HN_CORE_ASSERT(rp_native, "get_or_create_meshlet_gbuffer_pipeline: rp_native is null");
        PipelineVariantKey key{rp_native, 0, (uint8_t)(cull_none ? 1 : 0)};

        auto it = g_renderer3d_data->vk_meshlet_gbuffer_pipelines.find(key);
        if (it != g_renderer3d_data->vk_meshlet_gbuffer_pipelines.end())
            return it->second;

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_MeshletDeferred.glsl");
        spec.perColorAttachmentBlend.clear();
        spec.perColorAttachmentBlend.resize(3, AttachmentBlendState{});
        if (cull_none)
            spec.cullMode = CullMode::None;

        auto pipeline = Pipeline::create(spec, rp_native, extra_layout);
        g_renderer3d_data->vk_meshlet_gbuffer_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    void flush_meshlet_draws() {
        HN_PROFILE_FUNCTION();

        if (g_renderer3d_data->meshlet_draws.empty())
            return;

        if (!g_renderer3d_data->vk_context_cache) {
            auto* base = Application::get().get_window().get_context();
            g_renderer3d_data->vk_context_cache = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(g_renderer3d_data->vk_context_cache, "flush_meshlet_draws: expected VulkanContext");
        }
        auto* vk_ctx = g_renderer3d_data->vk_context_cache;

        void* rp_native = nullptr;
        if (auto target = Renderer::get_render_target()) {
            auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
            HN_CORE_ASSERT(vk_fb, "flush_meshlet_draws: render target is not a VulkanFramebuffer");
            rp_native = vk_fb->get_render_pass();
        } else {
            rp_native = vk_ctx->get_render_pass();
        }
        HN_CORE_ASSERT(rp_native, "flush_meshlet_draws: rpNative is null");

        void* extra_layout = VulkanRendererAPI::get_or_create_meshlet_set_layout();
        CameraUBO saved_camera = VulkanRendererAPI::get_globals_state().cameraUBO;

        auto& tex_slot_map = g_renderer3d_data->frame_tex_slot_map;
        auto& gpu_materials = g_renderer3d_data->frame_gpu_materials;
        tex_slot_map.clear();
        gpu_materials.clear();
        tex_slot_map[g_renderer3d_data->white_texture.get()] = 0;
        uint32_t tex_slot_count = 1;

        gpu_materials.reserve(g_renderer3d_data->meshlet_draws.size());
        for (const auto& cmd : g_renderer3d_data->meshlet_draws)
            gpu_materials.push_back(build_gpu_material(cmd.material, tex_slot_map, tex_slot_count));

        std::array<void*, VulkanRendererAPI::k_max_texture_slots> tex_array{};
        tex_array[0] = g_renderer3d_data->white_texture.get();
        for (auto& [tex_ptr, slot] : tex_slot_map)
            tex_array[slot] = tex_ptr;

        const bool deferred = Settings::get().renderer.renderer_type == RendererSettings::RendererType::deferred;

        if (!g_renderer3d_data->meshlet_draws.empty()) {
            const auto* first_mat = g_renderer3d_data->meshlet_draws[0].material;
            const bool blend = first_mat && first_mat->get_alpha_mode() == Material::AlphaMode::Blend;
            const bool cull_none = first_mat && first_mat->get_double_sided();
            Ref<Pipeline> first_pipe = deferred
                ? get_or_create_meshlet_gbuffer_pipeline(rp_native, extra_layout, cull_none)
                : get_or_create_meshlet_pipeline(rp_native, extra_layout, blend, cull_none);
            RenderCommand::bind_pipeline(first_pipe);
            g_renderer3d_data->stats.pipeline_binds++;
        }

        VulkanRendererAPI::submit_camera(saved_camera);
        VulkanRendererAPI::submit_bound_textures(tex_array, tex_slot_count);
        VulkanRendererAPI::submit_materials(gpu_materials, 0);
        VulkanRendererAPI::flush_globals();

        auto& mesh_order = g_renderer3d_data->frame_mesh_order;
        auto& draws_by_mesh = g_renderer3d_data->frame_draws_by_mesh;
        mesh_order.clear();
        draws_by_mesh.clear();

        for (uint32_t i = 0; i < (uint32_t)g_renderer3d_data->meshlet_draws.size(); ++i) {
            const Mesh* mesh = g_renderer3d_data->meshlet_draws[i].mesh;
            auto [it, inserted] = draws_by_mesh.try_emplace(mesh);
            if (inserted)
                mesh_order.push_back(mesh);
            it->second.push_back(i);
        }

        const uint32_t total_draws = (uint32_t)g_renderer3d_data->meshlet_draws.size();
        const uint32_t indirect_bytes = total_draws * 12u;
        const uint32_t count_bytes = std::max(total_draws, 1u) * (uint32_t)sizeof(uint32_t);

        const uint32_t frame_slot = vk_ctx->get_current_frame() % VulkanContext::k_max_frames_in_flight;
        auto& indirect_buffer = g_renderer3d_data->indirect_buffers[frame_slot];
        auto& count_buffer = g_renderer3d_data->count_buffers[frame_slot];

        if (!indirect_buffer || indirect_buffer->get_size() < indirect_bytes)
            indirect_buffer = StorageBuffer::create(indirect_bytes, StorageBufferUsage::Dynamic | StorageBufferUsage::Indirect);
        if (!count_buffer || count_buffer->get_size() < count_bytes)
            count_buffer = StorageBuffer::create(std::max(count_bytes, 4u), StorageBufferUsage::Dynamic | StorageBufferUsage::Indirect);

        auto indirect_vk = reinterpret_cast<VkBuffer>(indirect_buffer->get_native_buffer());
        auto count_vk = reinterpret_cast<VkBuffer>(count_buffer->get_native_buffer());

        auto& indirect_cmds = g_renderer3d_data->frame_indirect_cmds;
        auto& draw_data = g_renderer3d_data->frame_draw_data;

        uint32_t global_draw_offset = 0;
        uint32_t dispatch_counter = 0;
        Ref<Pipeline> current_pipe;

        for (uint32_t mesh_idx = 0; mesh_idx < (uint32_t)mesh_order.size(); ++mesh_idx) {
            const Mesh* mesh = mesh_order[mesh_idx];
            const auto& indices = draws_by_mesh.at(mesh);
            const uint32_t mesh_total_draws = (uint32_t)indices.size();
            uint32_t mesh_local_draw_offset = 0;

            HN_CORE_ASSERT(mesh && mesh->meshlet_buffers.has_value(),
                           "flush_meshlet_draws: mesh has no global meshlet buffers");
            auto& bufs = const_cast<GlobalMeshletBuffers&>(*mesh->meshlet_buffers);

            VulkanRendererAPI::update_mesh_draw_data_binding(bufs, mesh_total_draws);
            VulkanRendererAPI::ensure_mesh_descriptor_set(bufs);

            std::unordered_map<uint8_t, std::vector<uint32_t>> draws_by_variant;
            std::vector<uint8_t> variant_order;
            draws_by_variant.reserve(4);
            variant_order.reserve(4);

            for (uint32_t draw_idx : indices) {
                const auto* draw_mat = g_renderer3d_data->meshlet_draws[draw_idx].material;
                const bool blend = draw_mat && draw_mat->get_alpha_mode() == Material::AlphaMode::Blend;
                const bool cull_none = draw_mat && draw_mat->get_double_sided();
                const uint8_t variant = (uint8_t)((blend ? 1 : 0) | (cull_none ? 2 : 0));

                auto [it, inserted] = draws_by_variant.try_emplace(variant);
                if (inserted)
                    variant_order.push_back(variant);
                it->second.push_back(draw_idx);
            }

            for (uint8_t variant : variant_order) {
                const auto& variant_draws = draws_by_variant.at(variant);
                const uint32_t mesh_draw_count = (uint32_t)variant_draws.size();
                const bool blend = (variant & 1u) != 0u;
                const bool cull_none = (variant & 2u) != 0u;

                Ref<Pipeline> pipe = deferred
                    ? get_or_create_meshlet_gbuffer_pipeline(rp_native, extra_layout, cull_none)
                    : get_or_create_meshlet_pipeline(rp_native, extra_layout, blend, cull_none);
                if (pipe != current_pipe) {
                    RenderCommand::bind_pipeline(pipe);
                    g_renderer3d_data->stats.pipeline_binds++;
                    current_pipe = pipe;
                }

                indirect_cmds.clear();
                draw_data.clear();
                indirect_cmds.reserve(mesh_draw_count);
                draw_data.reserve(mesh_draw_count);

                const uint32_t mesh_draw_base = mesh_local_draw_offset;
                for (uint32_t local_i = 0; local_i < mesh_draw_count; ++local_i) {
                    const uint32_t draw_idx = variant_draws[local_i];
                    const auto& cmd = g_renderer3d_data->meshlet_draws[draw_idx];
                    const auto& geo = *cmd.submesh->meshlets;

                    indirect_cmds.push_back({geo.meshlet_count, 1, 1});
                    draw_data.push_back({
                        cmd.transform,
                        geo.meshlets_offset,
                        geo.meshlet_count,
                        (int32_t)draw_idx,
                        cmd.entity_id,
                    });
                }

                const uint32_t indirect_byte_off = global_draw_offset * 12u;
                const uint32_t count_byte_off = dispatch_counter * (uint32_t)sizeof(uint32_t);

                indirect_buffer->set_data(indirect_cmds.data(), mesh_draw_count * 12u, indirect_byte_off);
                count_buffer->set_data(&mesh_draw_count, sizeof(uint32_t), count_byte_off);

                Ref<StorageBuffer> draw_data_buffer = VulkanRendererAPI::get_mesh_draw_data_buffer(bufs);
                HN_CORE_ASSERT(draw_data_buffer, "flush_meshlet_draws: draw_data_buffer not available for frame slot");
                draw_data_buffer->set_data(
                    draw_data.data(),
                    mesh_draw_count * (uint32_t)sizeof(GPUDrawData),
                    mesh_draw_base * (uint32_t)sizeof(GPUDrawData));

                struct MeshletPC {
                    int32_t draw_data_base;
                    int32_t _pad[3];
                };

                const MeshletPC pc{(int32_t)mesh_draw_base, {0, 0, 0}};
                VulkanRendererAPI::submit_push_constants(
                    &pc,
                    sizeof(MeshletPC),
                    0,
                    VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT);

                VulkanRendererAPI::submit_set1_descriptor_set(
                    VulkanRendererAPI::get_mesh_descriptor_set(bufs),
                    pipe->get_native_pipeline_layout());

                VulkanRendererAPI::submit_mesh_tasks_indirect_count(
                    indirect_vk,
                    indirect_byte_off,
                    count_vk,
                    count_byte_off,
                    mesh_draw_count,
                    12u);

                g_renderer3d_data->stats.draw_calls++;
                mesh_local_draw_offset += mesh_draw_count;
                global_draw_offset += mesh_draw_count;
                dispatch_counter++;
            }
        }

        g_renderer3d_data->meshlet_draws.clear();
    }
}
