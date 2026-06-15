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

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_DeferredLighting.glsl");
            spec.depthStencil.depthTest = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(2, AttachmentBlendState{});

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
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

    void Renderer3D::flush_deferred_lighting(FrameGraphPassContext& ctx) {
        auto* data = Renderer3DInternal::g_renderer3d_data;
        HN_CORE_ASSERT(data, "Renderer3D not initialized");

        if (Renderer::get_api() != RendererAPI::API::vulkan)
            return;

        if (!data->vk_context_cache) {
            auto* base = Application::get().get_window().get_context();
            data->vk_context_cache = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(data->vk_context_cache, "flush_deferred_lighting: VulkanContext null");
        }
        auto* vk_ctx = data->vk_context_cache;

        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "flush_deferred_lighting: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();
        HN_CORE_ASSERT(rp_native, "flush_deferred_lighting: rpNative is null");

        Ref<Pipeline> pipe = Renderer3DInternal::get_or_create_lighting_pipeline(rp_native);
        VkPipeline vk_pipe = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        HN_CORE_ASSERT(vk_pipe, "flush_deferred_lighting: heap-mode pipeline is null");

        CameraUBO cam_ubo{};
        cam_ubo.view_proj = data->scene_view_proj;
        cam_ubo.view      = data->scene_view;
        cam_ubo.projection = data->scene_projection;
        cam_ubo.position  = data->scene_camera_pos;
        cam_ubo.exposure  = data->scene_camera_exposure;
        VulkanRendererAPI::submit_camera(cam_ubo);
        VulkanRendererAPI::submit_lights(data->scene_lights);
        VulkanRendererAPI::submit_tiled_lighting(data->scene_tiled_lighting);
        VulkanRendererAPI::flush_globals_to_heap();

        const VkExtent2D ext = vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw(
            [&](VkCommandBuffer cmd) {
                HN_GPU_SCOPE(cmd, "Deferred Lighting");
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
                vkCmdSetViewport(cmd, 0, 1, &vp);
                vkCmdSetScissor(cmd, 0, 1, &sc);
                ctx.bind_heap_pipeline(*pipe);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            });
    }
}
