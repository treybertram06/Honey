#include "hnpch.h"
#include "renderer_3d_ssao.h"

#include <random>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>

#include "renderer_3d_internal.h"
#include "Honey/core/engine.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/texture.h"
#include "Honey/renderer/buffer.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_texture.h"
#include "Honey/renderer/pipeline.h"

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {

        struct SSAOResources {
            VulkanContext* vk_ctx = nullptr;

            // Noise rotation texture (4x4 RGBA8, RG = random rotation vectors)
            Ref<Texture2D> noise_texture;

            // Kernel data now lives in the frame-graph buffer resource "ssaoKernel"; we only
            // fill its bytes (see execute_draw). The graph allocates a fresh, zero-filled
            // buffer on every rebuild, so the fill is keyed on the buffer instance (weak_ptr
            // survives address reuse) rather than a once-ever flag. noise_scale is re-uploaded
            // only when the render-target size changes (which goes through device-idle), so
            // the single, non-double-buffered buffer is hazard-free.
            std::weak_ptr<StorageBuffer> filled_kernel_buffer;
            glm::vec2 last_noise_scale{-1.0f, -1.0f};

            std::unordered_map<void*, Ref<Pipeline>> blur_pipelines;
            std::unordered_map<void*, Ref<Pipeline>> ssao_pipelines;
        };
        static SSAOResources* s_res = nullptr;

        Ref<Pipeline> get_or_create_ssao_pipeline(void* rp_native) {
            auto it = s_res->ssao_pipelines.find(rp_native);
            if (it != s_res->ssao_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_SSAO.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
            s_res->ssao_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }

        Ref<Pipeline> get_or_create_blur_pipeline(void* rp_native) {
            auto it = s_res->blur_pipelines.find(rp_native);
            if (it != s_res->blur_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_SSAOBlur.glsl");
            spec.depthStencil.depthTest  = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
            s_res->blur_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }


        static void generate_ssao_kernel(glm::vec4* out, uint32_t count) {
            std::mt19937 rng{42};
            std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
            std::uniform_real_distribution<float> distN1(-1.0f, 1.0f);

            for (uint32_t i = 0; i < count; ++i) {
                glm::vec3 s{distN1(rng), distN1(rng), dist01(rng)};
                s = glm::normalize(s) * dist01(rng);
                float scale = float(i) / float(count);
                scale = glm::mix(0.1f, 1.0f, scale * scale);
                out[i] = glm::vec4(s * scale, 0.0f);
            }
        }

        static void create_ssao_resources() {
            HN_PROFILE_FUNCTION();

            // The kernel UBO and every descriptor write are handled by the frame-graph
            // heap automation; the only GPU resource this subsystem still owns is the
            // imported noise rotation texture (4x4 RGBA8, RG = random rotation vectors).
            std::mt19937 rng{7};
            std::uniform_int_distribution<int> dist(0, 255);
            struct NoisePixel { uint8_t r, g, b, a; };
            std::array<NoisePixel, 16> noise_pixels{};
            for (auto& p : noise_pixels) {
                p.r = static_cast<uint8_t>(dist(rng));
                p.g = static_cast<uint8_t>(dist(rng));
                p.b = 0;
                p.a = 255;
            }
            s_res->noise_texture = Texture2D::create(4, 4);
            s_res->noise_texture->set_data(noise_pixels.data(), static_cast<uint32_t>(sizeof(noise_pixels)));
        }

        static void cleanup_ssao_resources() {
            HN_PROFILE_FUNCTION();
            // Noise texture is managed by Texture2D
            s_res->noise_texture.reset();
        }
    }

    void Renderer3DSSAO::init(VulkanContext* ctx) {
        if (s_res) return;
        s_res = new SSAOResources{};
        s_res->vk_ctx = ctx;
        create_ssao_resources();
    }

    void Renderer3DSSAO::shutdown() {
        if (!s_res) return;
        cleanup_ssao_resources();
        delete s_res;
        s_res = nullptr;
    }

    void Renderer3DSSAO::execute_draw(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* data    = Renderer3DInternal::g_renderer3d_data;
        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "execute_draw: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();

        Ref<Pipeline> pipe = get_or_create_ssao_pipeline(rp_native);
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

        // Fill the frame-graph-owned kernel buffer. Every graph rebuild (resize, swapchain
        // recreate, graph edit) allocates a brand-new zero-filled buffer, so re-upload the
        // kernel whenever the buffer instance changes — a once-ever flag leaves the new
        // buffer all zeros (radius/bias/samples = 0 → no occlusion anywhere).
        if (auto kernel_buf = ctx.get_buffer("ssaoKernel")) {
            const auto& spec = target->get_specification();
            const glm::vec2 noise_scale(float(spec.width) / 4.0f, float(spec.height) / 4.0f);

            if (s_res->filled_kernel_buffer.lock() != kernel_buf) {
                SSAOKernelUBOData ubo{};
                generate_ssao_kernel(ubo.samples, 32);
                ubo.radius      = 0.5f;
                // 0.025 view-units: the previous 0.075 wiped out all occlusion on
                // small-scale geometry (FlightHelmet-sized props) whose crevices are
                // shallower than the bias.
                ubo.bias        = 0.025f;
                ubo.noise_scale = noise_scale;
                kernel_buf->set_data(&ubo, sizeof(ubo), 0);
                s_res->filled_kernel_buffer = kernel_buf;
                s_res->last_noise_scale = noise_scale;
                HN_CORE_INFO("[SSAO] kernel uploaded to frame-graph buffer (radius={}, bias={})",
                             ubo.radius, ubo.bias);
            } else if (noise_scale != s_res->last_noise_scale) {
                kernel_buf->set_data(&noise_scale, sizeof(noise_scale),
                                     offsetof(SSAOKernelUBOData, noise_scale));
                s_res->last_noise_scale = noise_scale;
            }
        }

        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "SSAO Draw");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            ctx.bind_heap_pipeline(*pipe);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    void Renderer3DSSAO::execute_blur(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "execute_blur: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();

        Ref<Pipeline> pipe = get_or_create_blur_pipeline(rp_native);
        VkPipeline vk_pipe = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        HN_CORE_ASSERT(vk_pipe, "execute_blur: heap-mode pipeline is null");

        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "SSAO Blur");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            ctx.bind_heap_pipeline(*pipe);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        });
    }

    Ref<Texture2D> Renderer3DSSAO::get_noise_texture() {
        return s_res->noise_texture ? s_res->noise_texture : nullptr;
    }

    void Renderer3DSSAO::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();
        registry.register_executor("ssao.draw", [](FrameGraphPassContext& ctx) {
            execute_draw(ctx);
        });
        registry.register_executor("ssao.blur", [](FrameGraphPassContext& ctx) {
            execute_blur(ctx);
        });
    }

    bool Renderer3DSSAO::is_initialized() {
        return s_res != nullptr;
    }
}