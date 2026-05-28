#include "hnpch.h"
#include "renderer_3d.h"
#include "renderer_3d_internal.h"

#include "Honey/core/engine.h"
#include "Honey/renderer/render_command.h"
#include "Honey/renderer/renderer.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_gpu_profiler.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey::Renderer3DInternal {

    Ref<Pipeline> get_or_create_gbuffer_pipeline(void* rp_native, bool blend, bool cull_none) {
        PipelineVariantKey key{rp_native, 0, (uint8_t)(cull_none ? 1 : 0)};

        auto it = g_renderer3d_data->vk_gbuffer_pipelines.find(key);
        if (it != g_renderer3d_data->vk_gbuffer_pipelines.end())
            return it->second;

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_DeferredGeometry.glsl");
        spec.perColorAttachmentBlend.clear();
        spec.perColorAttachmentBlend.resize(3, AttachmentBlendState{});
        if (cull_none)
            spec.cullMode = CullMode::None;

        auto pipeline = Pipeline::create(spec, rp_native);
        g_renderer3d_data->vk_gbuffer_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    namespace {
        Ref<Pipeline> get_or_create_lighting_pipeline(void* rp_native) {
            PipelineVariantKey key{rp_native, 0, 0};
            auto it = g_renderer3d_data->vk_lighting_pipelines.find(key);
            if (it != g_renderer3d_data->vk_lighting_pipelines.end())
                return it->second;

            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "get_or_create_lighting_pipeline: VulkanContext is null");

            void* gbuffer_layout = vk->get_gbuffer_set_layout();

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_DeferredLighting.glsl");
            spec.depthStencil.depthTest = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(2, AttachmentBlendState{});

            auto pipeline = Pipeline::create(spec, rp_native, gbuffer_layout);
            g_renderer3d_data->vk_lighting_pipelines.emplace(key, pipeline);
            return pipeline;
        }
    }
}

namespace Honey {

    void Renderer3D::write_ssao_fb_to_renderer_state(Ref<Framebuffer> ssao_fb) {
        HN_CORE_ASSERT(Renderer3DInternal::g_renderer3d_data, "Renderer3D not initialized");
        Renderer3DInternal::g_renderer3d_data->current_ssao_fb = ssao_fb;
    }

    void Renderer3D::write_gbuffer_to_renderer_state(Ref<Framebuffer> gbuffer_fb) {
        HN_CORE_ASSERT(Renderer3DInternal::g_renderer3d_data, "Renderer3D not initialized");
        Renderer3DInternal::g_renderer3d_data->current_gbuffer_fb = gbuffer_fb;
    }

    void Renderer3D::flush_deferred_lighting() {
        auto* data = Renderer3DInternal::g_renderer3d_data;
        HN_CORE_ASSERT(data, "Renderer3D not initialized");
        HN_CORE_ASSERT(data->current_gbuffer_fb,
                        "flush_deferred_lighting: no gbuffer_fb set - call write_gbuffer_to_renderer_state first");
        HN_CORE_ASSERT(data->current_ssao_fb,
                        "flush_deferred_lighting: no ssao_fb set - call write_ssao_fb_to_renderer_state first");

        if (Renderer::get_api() != RendererAPI::API::vulkan)
            return;

        if (!data->vk_context_cache) {
            auto* base = Application::get().get_window().get_context();
            data->vk_context_cache = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(data->vk_context_cache, "flush_deferred_lighting: VulkanContext null");
        }
        auto* vk_ctx = data->vk_context_cache;

        void* rp_native = nullptr;
        if (auto target = Renderer::get_render_target()) {
            auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
            HN_CORE_ASSERT(vk_fb, "flush_deferred_lighting: render target is not VulkanFramebuffer");
            rp_native = vk_fb->get_render_pass();
        } else {
            rp_native = vk_ctx->get_render_pass();
        }
        HN_CORE_ASSERT(rp_native, "flush_deferred_lighting: rpNative is null");

        Ref<Pipeline> pipe = Renderer3DInternal::get_or_create_lighting_pipeline(rp_native);
        RenderCommand::bind_pipeline(pipe);

        CameraUBO cam_ubo{};
        cam_ubo.view_proj = data->scene_view_proj;
        cam_ubo.view      = data->scene_view;
        cam_ubo.projection = data->scene_projection;
        cam_ubo.position  = data->scene_camera_pos;
        cam_ubo.exposure  = data->scene_camera_exposure;
        VulkanRendererAPI::submit_camera(cam_ubo);
        VulkanRendererAPI::submit_lights(data->scene_lights);
        VulkanRendererAPI::submit_tiled_lighting(data->scene_tiled_lighting);

        std::array<void*, VulkanRendererAPI::k_max_texture_slots> tex_array{};
        tex_array[0] = data->white_texture.get();
        VulkanRendererAPI::submit_bound_textures(tex_array, 1);
        VulkanRendererAPI::flush_globals();

        VkPipelineLayout pipe_layout = static_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
        Ref<Framebuffer> gbuffer_fb = data->current_gbuffer_fb;
        Ref<Framebuffer> ssao_fb = data->current_ssao_fb;

        vk_ctx->queue_custom_vulkan_cmd(
            [vk_ctx, gbuffer_fb, ssao_fb, pipe_layout](VkCommandBuffer cmd, uint32_t, uint32_t) {
                HN_GPU_SCOPE(cmd, "Deferred Lighting");

                auto* gbuffer_vk = dynamic_cast<VulkanFramebuffer*>(gbuffer_fb.get());
                HN_CORE_ASSERT(gbuffer_vk, "flush_deferred_lighting: current_gbuffer_fb is not a VulkanFramebuffer");
                auto* ssao_vk = dynamic_cast<VulkanFramebuffer*>(ssao_fb.get());
                HN_CORE_ASSERT(ssao_vk, "flush_deferred_lighting: current_ssao_fb is not a VulkanFramebuffer");

                uint32_t frame = vk_ctx->get_current_frame();
                vk_ctx->update_gbuffer_descriptors(frame, gbuffer_vk);

                VkDescriptorSet gbuf_ds = vk_ctx->get_gbuffer_descriptor_set(frame);
                VkDescriptorImageInfo ssao_info{};
                ssao_info.sampler     = vk_ctx->get_backend()->get_sampler_linear();
                ssao_info.imageView   = ssao_vk->get_color_image_view(0);
                ssao_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet ssao_write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                ssao_write.dstSet          = gbuf_ds;
                ssao_write.dstBinding      = 6;
                ssao_write.descriptorCount = 1;
                ssao_write.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                ssao_write.pImageInfo      = &ssao_info;
                vkUpdateDescriptorSets(reinterpret_cast<VkDevice>(vk_ctx->get_device()), 1, &ssao_write, 0, nullptr);

                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout, 1, 1, &gbuf_ds, 0, nullptr);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            });
    }
}
