#include "hnpch.h"
#include "renderer_3d_postprocess.h"

#include "renderer_3d_internal.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/pipeline.h"
#include "Honey/renderer/pipeline_spec.h"
#include "platform/vulkan/vk_framebuffer.h"

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {

        struct PostProcessResources {
            VulkanContext* vk_ctx = nullptr;

            std::unordered_map<void*, Ref<Pipeline>> postprocess_pipelines;
        };
        static PostProcessResources* s_res = nullptr;

        Ref<Pipeline> get_or_create_postprocess_pipeline(void* rp_native) {
            auto it = s_res->postprocess_pipelines.find(rp_native);
            if (it != s_res->postprocess_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_PostProcessComposite.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
            s_res->postprocess_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }
    }

    void Renderer3DPostProcess::init(VulkanContext* ctx) {
        if (s_res) return;
        s_res = new PostProcessResources{};
        s_res->vk_ctx = ctx;
    }

    void Renderer3DPostProcess::shutdown() {
        if (!s_res) return;
        delete s_res;
        s_res = nullptr;
    }

    void Renderer3DPostProcess::execute_draw(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* data    = Renderer3DInternal::g_renderer3d_data;
        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "execute_draw: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();

        Ref<Pipeline> pipe = get_or_create_postprocess_pipeline(rp_native);
        VkPipeline vk_pipe = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        HN_CORE_ASSERT(vk_pipe, "execute_draw: heap-mode pipeline is null");

        // set=0 camera global → persistent heap slot
        CameraUBO cam_ubo{};
        cam_ubo.view_proj  = data->scene_view_proj;
        cam_ubo.view       = data->scene_view;
        cam_ubo.projection = data->scene_projection;
        cam_ubo.position   = data->scene_camera_pos;
        cam_ubo.exposure   = data->scene_camera_exposure;
        VulkanRendererAPI::submit_camera(cam_ubo);
        VulkanRendererAPI::flush_globals_to_heap();

        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "Postprocess");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            ctx.bind_heap_pipeline(*pipe);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });

    }

    void Renderer3DPostProcess::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();
        registry.register_executor("postprocess.composite", [](FrameGraphPassContext& ctx) {
            execute_draw(ctx);
        });
    }

    bool Renderer3DPostProcess::is_initialized() {
        return s_res != nullptr;
    }

}
