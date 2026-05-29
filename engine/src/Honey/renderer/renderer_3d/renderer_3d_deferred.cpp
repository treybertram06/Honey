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
        struct GBufferDescriptors {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VkDescriptorPool      pool   = VK_NULL_HANDLE;
            VkDescriptorSet       sets[VulkanContext::k_max_frames_in_flight]{};

            VulkanFramebuffer* last_fb[VulkanContext::k_max_frames_in_flight]{};
            uint64_t           last_fb_gen[VulkanContext::k_max_frames_in_flight]{};

            void init(VkDevice device) {
                HN_PROFILE_FUNCTION();
                HN_CORE_ASSERT(device, "GBufferDescriptors::init() called without device");

                // 7 bindings: gAlbedo (b=0), gNormal (b=1), gPBRParams (b=2), gDepth (b=3), shadowCubemap (b=4), directionalShadow (b=5), SSAO (b=6)
                // All COMBINED_IMAGE_SAMPLER, FRAGMENT stage.
                static constexpr uint32_t k_gbuffer_binding_count = 7;

                VkDescriptorSetLayoutBinding bindings[k_gbuffer_binding_count]{};
                for (uint32_t i = 0; i < k_gbuffer_binding_count; ++i) {
                    bindings[i].binding         = i;
                    bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    bindings[i].descriptorCount = 1;
                    bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
                }

                VkDescriptorBindingFlags binding_flags[k_gbuffer_binding_count]{};  // all zero

                VkDescriptorSetLayoutBindingFlagsCreateInfo binding_flags_ci{};
                binding_flags_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
                binding_flags_ci.bindingCount  = k_gbuffer_binding_count;
                binding_flags_ci.pBindingFlags = binding_flags;

                VkDescriptorSetLayoutCreateInfo layout_ci{};
                layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
                layout_ci.pNext        = &binding_flags_ci;
                layout_ci.bindingCount = k_gbuffer_binding_count;
                layout_ci.pBindings    = bindings;

                {
                    VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
                    VkResult r = vkCreateDescriptorSetLayout(reinterpret_cast<VkDevice>(device), &layout_ci, nullptr, &set_layout);
                    HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorSetLayout (gbuffer) failed");
                    layout = set_layout;
                }

                {
                    VkDescriptorPoolSize pool_size{};
                    pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    pool_size.descriptorCount = VulkanContext::k_max_frames_in_flight * k_gbuffer_binding_count;

                    VkDescriptorPoolCreateInfo pool_ci{};
                    pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
                    pool_ci.maxSets       = VulkanContext::k_max_frames_in_flight;
                    pool_ci.poolSizeCount = 1;
                    pool_ci.pPoolSizes    = &pool_size;

                    VkResult r = vkCreateDescriptorPool(reinterpret_cast<VkDevice>(device), &pool_ci, nullptr, &pool);
                    HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateDescriptorPool (gbuffer) failed");
                }

                std::vector<VkDescriptorSetLayout> layouts(VulkanContext::k_max_frames_in_flight, layout);

                VkDescriptorSetAllocateInfo alloc{};
                alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                alloc.descriptorPool     = pool;
                alloc.descriptorSetCount = VulkanContext::k_max_frames_in_flight;
                alloc.pSetLayouts        = layouts.data();

                std::vector<VkDescriptorSet> vk_sets(VulkanContext::k_max_frames_in_flight);
                VkResult r = vkAllocateDescriptorSets(reinterpret_cast<VkDevice>(device), &alloc, vk_sets.data());
                HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateDescriptorSets (gbuffer) failed");

                for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
                    sets[f]        = vk_sets[f];
                    last_fb[f]     = nullptr;
                    last_fb_gen[f] = 0;
                }
            }
            void shutdown(VkDevice device) {
                HN_PROFILE_FUNCTION();
                if (!device) return;

                for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
                    sets[f]    = VK_NULL_HANDLE;
                    last_fb[f] = nullptr;
                    last_fb_gen[f] = 0;
                }

                if (pool) {
                    vkDestroyDescriptorPool(reinterpret_cast<VkDevice>(device),
                                            reinterpret_cast<VkDescriptorPool>(pool), nullptr);
                    pool = nullptr;
                }
                if (layout) {
                    vkDestroyDescriptorSetLayout(reinterpret_cast<VkDevice>(device),
                                                 reinterpret_cast<VkDescriptorSetLayout>(layout), nullptr);
                    layout = nullptr;
                }
            }
            void update(uint32_t frame, VkDevice device, VkSampler linear,
                        VulkanFramebuffer* gbuffer_fb,
                        VulkanFramebuffer* shadow_cube_fb,
                        VulkanFramebuffer* shadow_dir_fb) {
                HN_CORE_ASSERT(frame < VulkanContext::k_max_frames_in_flight, "GBufferDescriptors::update(): frame index out of range");
                HN_CORE_ASSERT(gbuffer_fb, "GBufferDescriptors::update(): fb is null");
                HN_CORE_ASSERT(sets[frame] != VK_NULL_HANDLE, "GBufferDescriptors::update(): descriptor set not allocated");

                const uint64_t fb_generation = gbuffer_fb->get_resource_generation();
                if (gbuffer_fb == last_fb[frame] &&
                    fb_generation == last_fb_gen[frame])
                    return;  // already up-to-date for this frame

                HN_CORE_ASSERT(linear, "GBufferDescriptors::update(): sampler is null");

                // Build 6 image infos: gAlbedo (0), gNormal (1), gPBRParams (2), gDepth (3),
                // shadowCubemap (4), directionalShadow (5)
                // Color attachments are in SHADER_READ_ONLY_OPTIMAL after the G-buffer pass.
                // Depth is in DEPTH_STENCIL_READ_ONLY_OPTIMAL (finalLayout changed in vk_framebuffer.cpp).
                VkDescriptorImageInfo image_infos[6]{};
                for (uint32_t i = 0; i < 3; ++i) {
                    image_infos[i].sampler     = linear;
                    image_infos[i].imageView   = gbuffer_fb->get_color_image_view(i);
                    image_infos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
                image_infos[3].sampler     = linear;
                image_infos[3].imageView   = gbuffer_fb->get_depth_sampler_image_view();
                image_infos[3].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                // binding 4: shadow cubemap. Must use get_cube_array_view() (VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
                // to match samplerCubeArray in the shader; get_depth_sampler_image_view() is 2D_ARRAY which
                // violates VUID-vkCmdDraw-viewType-07752. Use comparison sampler presence as the guard since
                // it implies cube_compatible (get_cube_array_view() asserts if not cube_compatible).
                {
                    VkSampler cube_cmp = shadow_cube_fb->get_depth_comparison_sampler();
                    image_infos[4].sampler   = cube_cmp ? cube_cmp : linear;
                    image_infos[4].imageView = cube_cmp ? shadow_cube_fb->get_cube_array_view()
                                                        : gbuffer_fb->get_depth_sampler_image_view();
                }
                image_infos[4].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                // binding 5: directional shadow map.
                // Falls back to a dummy (linear sampler, gDepth view) when shadow resources aren't set yet.
                image_infos[5].sampler     = shadow_dir_fb->get_depth_comparison_sampler()
                ? shadow_dir_fb->get_depth_comparison_sampler() : linear;
                image_infos[5].imageView   = shadow_dir_fb->get_depth_sampler_image_view()
                ? shadow_dir_fb->get_depth_sampler_image_view() : gbuffer_fb->get_depth_sampler_image_view();
                image_infos[5].imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

                constexpr uint32_t write_count = 6u;
                VkWriteDescriptorSet writes[6]{};
                for (uint32_t i = 0; i < write_count; ++i) {
                    writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    writes[i].dstSet          = sets[frame];
                    writes[i].dstBinding      = i;
                    writes[i].dstArrayElement = 0;
                    writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    writes[i].descriptorCount = 1;
                    writes[i].pImageInfo      = &image_infos[i];
                }

                vkUpdateDescriptorSets(reinterpret_cast<VkDevice>(device), write_count, writes, 0, nullptr);
                last_fb[frame] = gbuffer_fb;
                last_fb_gen[frame] = fb_generation;
            }
        };
        static GBufferDescriptors* s_gbuf = nullptr;

        Ref<Pipeline> get_or_create_lighting_pipeline(void* rp_native) {
            PipelineVariantKey key{rp_native, 0, 0};
            auto it = g_renderer3d_data->vk_lighting_pipelines.find(key);
            if (it != g_renderer3d_data->vk_lighting_pipelines.end())
                return it->second;

            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "get_or_create_lighting_pipeline: VulkanContext is null");

            if (!s_gbuf) {
                s_gbuf = new GBufferDescriptors();
                s_gbuf->init(vk->get_device());
            }

            void* gbuffer_layout = s_gbuf->layout;

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


    void shutdown_gbuffer_descriptors() {
        if (!s_gbuf) return;
        auto* base = Application::get().get_window().get_context();
        if (auto* vk = dynamic_cast<VulkanContext*>(base))
            s_gbuf->shutdown(vk->get_device());
        delete s_gbuf;
        s_gbuf = nullptr;
    }

    void invalidate_gbuffer_descriptors() {
        if (!s_gbuf) return;
        for (uint32_t f = 0; f < VulkanContext::k_max_frames_in_flight; ++f) {
            s_gbuf->last_fb[f]     = nullptr;
            s_gbuf->last_fb_gen[f] = 0;
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

    void Renderer3D::flush_deferred_lighting(Ref<Framebuffer> shadow_cube_fb, Ref<Framebuffer> shadow_dir_fb) {
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
            [vk_ctx, gbuffer_fb, ssao_fb, pipe_layout, shadow_cube_fb, shadow_dir_fb](VkCommandBuffer cmd, uint32_t, uint32_t) {
                HN_GPU_SCOPE(cmd, "Deferred Lighting");

                auto* gbuffer_vk = dynamic_cast<VulkanFramebuffer*>(gbuffer_fb.get());
                HN_CORE_ASSERT(gbuffer_vk, "flush_deferred_lighting: current_gbuffer_fb is not a VulkanFramebuffer");
                auto* ssao_vk = dynamic_cast<VulkanFramebuffer*>(ssao_fb.get());
                HN_CORE_ASSERT(ssao_vk, "flush_deferred_lighting: current_ssao_fb is not a VulkanFramebuffer");
                auto* shadow_cube_vk = dynamic_cast<VulkanFramebuffer*>(shadow_cube_fb.get());
                HN_CORE_ASSERT(shadow_cube_vk, "flush_deferred_lighting: shadow_cube_fb is not a VulkanFramebuffer");
                auto* shadow_dir_vk = dynamic_cast<VulkanFramebuffer*>(shadow_dir_fb.get());
                HN_CORE_ASSERT(shadow_dir_vk, "flush_deferred_lighting: shadow_dir_fb is not a VulkanFramebuffer");

                uint32_t frame = vk_ctx->get_current_frame();
                Renderer3DInternal::s_gbuf->update(frame, vk_ctx->get_device(), vk_ctx->get_backend()->get_sampler_linear(), gbuffer_vk, shadow_cube_vk, shadow_dir_vk);
                VkDescriptorSet gbuf_ds = Renderer3DInternal::s_gbuf->sets[frame];

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
