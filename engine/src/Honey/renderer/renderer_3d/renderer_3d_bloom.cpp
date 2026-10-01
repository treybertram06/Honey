#include "hnpch.h"
#include "renderer_3d_bloom.h"

#include "renderer_3d_internal.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/pipeline.h"
#include "Honey/renderer/pipeline_spec.h"
#include "platform/vulkan/vk_framebuffer.h"

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {
        struct BloomResources {
            VulkanContext* vk_ctx = nullptr;

            std::weak_ptr<StorageBuffer> filled_bloom_params_buffer;
            RendererSettings::BloomSettings last_bloom_settings;

            std::unordered_map<void*, Ref<Pipeline>> threshold_pipelines;
            std::unordered_map<void*, Ref<Pipeline>> downsample_pipelines;
            std::unordered_map<void*, Ref<Pipeline>> upsample_pipelines;
        };
        static BloomResources* s_res = nullptr;

        Ref<Pipeline> get_or_create_threshold_pipeline(void* rp_native) {
            auto it = s_res->threshold_pipelines.find(rp_native);
            if (it != s_res->threshold_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "postprocessing" / "Renderer3D_BloomThreshold.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
            s_res->threshold_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }

        Ref<Pipeline> get_or_create_downsample_pipeline(void* rp_native) {
            auto it = s_res->downsample_pipelines.find(rp_native);
            if (it != s_res->downsample_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "postprocessing" / "Renderer3D_BloomDownsample.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
            s_res->downsample_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }

        Ref<Pipeline> get_or_create_upsample_pipeline(void* rp_native) {
            auto it = s_res->upsample_pipelines.find(rp_native);
            if (it != s_res->upsample_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "postprocessing" / "Renderer3D_BloomUpsample.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
            s_res->upsample_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }
    }

    void Renderer3DBloom::init(VulkanContext* ctx) {
        if (s_res) return;
        s_res = new BloomResources{};
        s_res->vk_ctx = ctx;
    }

    void Renderer3DBloom::shutdown() {
        if (!s_res) return;
        delete s_res;
        s_res = nullptr;
    }

    void Renderer3DBloom::execute_threshold(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* data    = Renderer3DInternal::g_renderer3d_data;
        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "execute_draw: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();

        Ref<Pipeline> pipe = get_or_create_threshold_pipeline(rp_native);
        VkPipeline vk_pipe = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        HN_CORE_ASSERT(vk_pipe, "execute_draw: heap-mode pipeline is null");

        if (auto kernel_buf = ctx.get_buffer("bloomParams")) {
            const auto& settings = Settings::get().renderer.bloom;

            if (s_res->filled_bloom_params_buffer.lock() != kernel_buf
                || settings != s_res->last_bloom_settings) {
                s_res->last_bloom_settings = settings;

                BloomParamsUBOData ubo{};
                ubo.threshold = settings.threshold;
                ubo.soft_knee = settings.soft_knee;
                ubo.strength = settings.strength;

                kernel_buf->set_data(&ubo, sizeof(ubo), 0);
                s_res->filled_bloom_params_buffer = kernel_buf;

                //HN_CORE_INFO("[Bloom] Bloom parameters uploaded to frame-graph buffer "
                //             "(threshold={}, soft knee={}, strength={}", ubo.threshold, ubo.soft_knee, ubo.strength);
            }
        }

        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "Bloom Threshold");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            ctx.bind_heap_pipeline(*pipe);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    void Renderer3DBloom::execute_downsample(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "execute_draw: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();

        Ref<Pipeline> pipe = get_or_create_downsample_pipeline(rp_native);
        VkPipeline vk_pipe = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        HN_CORE_ASSERT(vk_pipe, "execute_draw: heap-mode pipeline is null");

        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "Bloom Downsample");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            ctx.bind_heap_pipeline(*pipe);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    void Renderer3DBloom::execute_upsample(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "execute_draw: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();

        Ref<Pipeline> pipe = get_or_create_upsample_pipeline(rp_native);
        VkPipeline vk_pipe = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        HN_CORE_ASSERT(vk_pipe, "execute_draw: heap-mode pipeline is null");

        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "Bloom Upsample");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            ctx.bind_heap_pipeline(*pipe);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    void Renderer3DBloom::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();
        registry.register_executor("bloom.threshold", [](FrameGraphPassContext& ctx) {
            execute_threshold(ctx);
        });
        registry.register_executor("bloom.upsample", [](FrameGraphPassContext& ctx) {
            execute_upsample(ctx);
        });
        registry.register_executor("bloom.downsample", [](FrameGraphPassContext& ctx) {
            execute_downsample(ctx);
        });
    }

    bool Renderer3DBloom::is_initialized() {
        return s_res != nullptr;
    }

}
