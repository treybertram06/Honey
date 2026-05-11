#include "hnpch.h"

#include "renderer_3d_shadow.h"
#include "renderer_3d_internal.h"

#include "Honey/renderer/pipeline.h"
#include "Honey/renderer/pipeline_spec.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/core/engine.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_renderer_api.h"

#include <vulkan/vulkan.h>
#include <filesystem>

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {
        struct ShadowResources {
            VulkanContext* vk_ctx = nullptr;
            Ref<Pipeline>  shadow_pipeline_ref;                  // keeps VkPipeline alive
            VkPipeline     shadow_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout shadow_layout = VK_NULL_HANDLE;
            bool           cubemap_first_frame = true;
            bool           shadow_resources_registered = false;

            uint32_t shadow_light_count = 0;
            ShadowMatricesSSBO ssbo_data{};
        };

        static ShadowResources* s_res = nullptr;

        static PFN_vkCmdDrawMeshTasksEXT s_fn_draw_mesh_tasks = nullptr;

        glm::mat4 make_shadow_face_view(glm::vec3 pos, uint32_t face) {
            // Face order matches Vulkan cubemap layer convention (spec section 16.5.4):
            // 0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z
            // Up vectors are chosen so that without a projection Y-flip the rendered (s,t)
            // per-texel coordinates match the GPU's samplerCubeArrayShadow lookup formula.
            static const glm::vec3 targets[6] = {
                { 1, 0, 0}, {-1, 0, 0}, { 0, 1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}
            };
            static const glm::vec3 ups[6] = {
                { 0,-1, 0}, { 0,-1, 0}, { 0, 0, 1}, { 0, 0,-1}, { 0,-1, 0}, { 0,-1, 0}
            };
            return glm::lookAt(pos, pos + targets[face], ups[face]);
        }

        glm::mat4 make_shadow_projection(float range) {
            // No Y-flip: the cubemap (s,t) sampling formula in the Vulkan spec is already
            // consistent with GLM's default RH_ZO NDC → texel mapping. Flipping Y would
            // invert the V coordinate within every face, making shadow lookups sample the
            // wrong row. Removing the flip also restores correct front-face culling (the flip
            // reverses winding order, causing back faces to be culled instead of front faces).
            return glm::perspectiveRH_ZO(glm::radians(90.0f), 1.0f, 0.05f, range);
        }

        void prepare_shadow_data(uint32_t frame, const LightsUBO& lights) {
            auto* res = s_res;
            const int total_lights = lights.directional_light.point_light_count;
            res->shadow_light_count = 0;

            auto& ssbo = res->ssbo_data;
            ssbo.shadow_light_count = 0;
            ssbo._pad[0] = ssbo._pad[1] = ssbo._pad[2] = 0;

            for (int i = 0; i < total_lights && res->shadow_light_count < k_max_shadow_lights; ++i) {
                const auto& pl = lights.point_lights[i];
                if (pl.intensity <= 0.0f) continue;

                uint32_t slot = res->shadow_light_count++;
                ssbo.shadow_light_point_indices[slot] = (uint32_t)i;

                glm::mat4 proj = make_shadow_projection(pl.range);
                for (uint32_t fi = 0; fi < 6; ++fi) {
                    glm::mat4 view = make_shadow_face_view(pl.position, fi);
                    ssbo.lights[slot].face_view_proj[fi] = proj * view;
                }
                ssbo.lights[slot].light_position = pl.position;
                ssbo.lights[slot].light_range     = pl.range;
            }
            ssbo.shadow_light_count = res->shadow_light_count;

            res->vk_ctx->upload_shadow_matrices(frame, ssbo);
        }
    } // anonymous namespace

    void Renderer3DShadow::init(VulkanContext* ctx) {
        if (s_res) return;
        s_res = new ShadowResources{};
        s_res->vk_ctx = ctx;

        s_fn_draw_mesh_tasks = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
            vkGetDeviceProcAddr(ctx->get_device(), "vkCmdDrawMeshTasksEXT"));
        HN_CORE_ASSERT(s_fn_draw_mesh_tasks,
            "Renderer3DShadow: vkCmdDrawMeshTasksEXT not available — do not call init() without mesh shader support");
    }

    void Renderer3DShadow::shutdown() {
        if (!s_res) return;

        delete s_res;
        s_res = nullptr;
        s_fn_draw_mesh_tasks = nullptr;
    }

    bool Renderer3DShadow::is_initialized() { return s_res != nullptr; }

    void Renderer3DShadow::execute_draw(FrameGraphPassContext& ctx) {
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* res = s_res;
        const uint32_t frame = ctx.frame_index() % VulkanContext::k_max_frames_in_flight;

        // Register shadow cubemap resources on the first frame so the deferred lighting descriptor
        // at set=1 binding=4 always has a valid cube-array + comparison sampler, even when there
        // are no shadow-casting lights yet (avoids VK_IMAGE_VIEW_TYPE_2D vs Cube validation error).
        if (!res->shadow_resources_registered) {
            VkImageView  cube_view    = ctx.get_resource_cube_array_view("shadowCubemap");
            VkSampler    cmp_sampler  = ctx.get_resource_depth_comparison_sampler("shadowCubemap");
            if (cube_view && cmp_sampler) {
                res->vk_ctx->set_shadow_cubemap_resources(cube_view, cmp_sampler);
                res->shadow_resources_registered = true;
            }
        }

        // Build shadow matrices from current scene lights and upload.
        prepare_shadow_data(frame, Renderer3DInternal::g_renderer3d_data->scene_lights);

        // On the very first frame, transition all cubemap layers to SHADER_READ so the deferred
        // lighting pass can safely sample them even when no shadow lights exist yet.
        if (res->cubemap_first_frame) {
            VkImage image = ctx.get_resource_vk_image("shadowCubemap");
            ctx.submit_vulkan_graphics_raw([image](VkCommandBuffer cmd) {
                VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                imb.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                imb.newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                imb.image            = image;
                imb.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, k_max_shadow_lights * 6 };
                imb.srcAccessMask    = 0;
                imb.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &imb);
            });
            res->cubemap_first_frame = false;
        }

        if (res->shadow_light_count == 0) return;

        // Lazily create the shadow pipeline on the first draw.
        if (!res->shadow_pipeline) {
            VkRenderPass shadow_rp = ctx.get_resource_render_pass("shadowCubemap");
            if (!shadow_rp) {
                HN_CORE_WARN("Renderer3DShadow: shadow render pass not available, skipping");
                return;
            }

            void* meshlet_extra_layout = VulkanRendererAPI::get_or_create_meshlet_set_layout();

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_ShadowCubemap.glsl");
            spec.pipelineKind = PipelineKind::MeshShading;
            spec.perColorAttachmentBlend.clear(); // depth-only — no color attachments
            spec.depthStencil.depthTest  = true;
            spec.depthStencil.depthWrite = true;
            // Without the projection Y-flip, world-CCW (front) faces project to CW in Vulkan's
            // Y-down viewport, so Vulkan sees them as "back faces". CullMode::Front therefore
            // culls Vulkan-front = viewport-CCW = world-back faces, leaving world-front faces
            // rendered — the standard shadow map approach.
            // Rasterizer depth bias prevents self-shadowing acne on the front faces.
            spec.cullMode                = CullMode::Front;
            spec.depthBiasConstantFactor = 2.0f;
            spec.depthBiasSlopeFactor    = 1.5f;

            Ref<Pipeline> pipe = Pipeline::create(spec, shadow_rp, meshlet_extra_layout);
            HN_CORE_ASSERT(pipe, "Renderer3DShadow: failed to create shadow pipeline");

            res->shadow_pipeline_ref = pipe;  // keeps VkPipeline alive past this scope
            res->shadow_pipeline = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
            res->shadow_layout   = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
        }

        if (!res->shadow_pipeline) return;
        if (Renderer3DInternal::g_renderer3d_data->shadow_draw_list.empty()) return;

        VkRenderPass rp    = ctx.get_resource_render_pass("shadowCubemap");
        VkImage      image = ctx.get_resource_vk_image("shadowCubemap");

        const uint32_t res_size = k_shadow_map_resolution;

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            // Transition READ_ONLY → DEPTH_ATTACHMENT. cubemap_first_frame was already cleared
            // by the initial transition above, so oldLayout is always DEPTH_STENCIL_READ_ONLY.
            VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            imb.oldLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            imb.newLayout           = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            imb.image               = image;
            imb.subresourceRange    = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, k_max_shadow_lights * 6 };
            imb.srcAccessMask       = VK_ACCESS_SHADER_READ_BIT;
            imb.dstAccessMask       = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                    | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &imb);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, res->shadow_pipeline);

            // Bind the global descriptor set at set=0 — the shadow matrices SSBO is at binding 6.
            VkDescriptorSet global_ds = res->vk_ctx->get_global_descriptor_set(frame);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                res->shadow_layout, 0, 1, &global_ds, 0, nullptr);

            const VkViewport vp{0.0f, 0.0f, (float)res_size, (float)res_size, 0.0f, 1.0f};
            const VkRect2D   sc{{0, 0}, {res_size, res_size}};

            for (uint32_t li = 0; li < res->shadow_light_count; ++li) {
                for (uint32_t fi = 0; fi < 6; ++fi) {
                    const uint32_t layer = li * 6 + fi;
                    VkFramebuffer fb = ctx.get_resource_layer_framebuffer("shadowCubemap", layer);
                    if (!fb) continue;

                    VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                    rpbi.renderPass        = rp;
                    rpbi.framebuffer       = fb;
                    rpbi.renderArea.extent = {res_size, res_size};
                    VkClearValue cv{};
                    cv.depthStencil        = {1.0f, 0};
                    rpbi.clearValueCount   = 1;
                    rpbi.pClearValues      = &cv;

                    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
                    vkCmdSetViewport(cmd, 0, 1, &vp);
                    vkCmdSetScissor(cmd, 0, 1, &sc);

                    for (const auto& draw : Renderer3DInternal::g_renderer3d_data->shadow_draw_list) {
                        // Bind the per-mesh descriptor set at set=1.
                        VkDescriptorSet mesh_ds = reinterpret_cast<VkDescriptorSet>(draw.mesh_descriptor_set);
                        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            res->shadow_layout, 1, 1, &mesh_ds, 0, nullptr);

                        ShadowDrawPC pc{draw.draw_data_base, (int32_t)li, fi, 0};
                        vkCmdPushConstants(cmd, res->shadow_layout,
                            VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                            0, sizeof(ShadowDrawPC), &pc);

                        s_fn_draw_mesh_tasks(cmd, draw.meshlet_count, 1, 1);
                    }

                    vkCmdEndRenderPass(cmd);
                }
            }

            // Transition all layers back to SHADER_READ for the deferred lighting pass.
            imb.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            imb.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            imb.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &imb);
        });
    }

    void Renderer3DShadow::invalidate_cubemap_resources() {
        if (!s_res) return;
        // Clear registration so execute_draw re-fetches views from the new frame graph next frame.
        s_res->shadow_resources_registered = false;
        s_res->cubemap_first_frame = true;
        // Discard the pipeline: it was created against the old frame graph's render pass.
        // A new one will be created lazily on the first draw after rebuild.
        s_res->shadow_pipeline_ref.reset();
        s_res->shadow_pipeline = VK_NULL_HANDLE;
        s_res->shadow_layout   = VK_NULL_HANDLE;
        // Clear stale handles in VulkanContext before the old framebuffer is destroyed.
        if (s_res->vk_ctx)
            s_res->vk_ctx->set_shadow_cubemap_resources(VK_NULL_HANDLE, VK_NULL_HANDLE);
    }

    // -------------------------------------------------------------------------
    // Directional light shadow map
    // -------------------------------------------------------------------------

    namespace {
        struct DirShadowResources {
            Ref<Pipeline> pipeline_ref;
            VkPipeline    pipeline    = VK_NULL_HANDLE;
            VkPipelineLayout layout   = VK_NULL_HANDLE;
            bool resources_registered = false;
            bool first_frame          = true;
        };

        static DirShadowResources* s_dir = nullptr;

        glm::mat4 compute_dir_shadow_vp(glm::vec3 light_dir, glm::vec3 cam_pos, float half_size) {
            glm::vec3 L  = glm::normalize(-light_dir);
            glm::vec3 up = (glm::abs(L.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                     : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::mat4 view = glm::lookAt(cam_pos + L * half_size, cam_pos, up);
            glm::mat4 proj = glm::orthoRH_ZO(-half_size, half_size,
                                              -half_size, half_size,
                                               0.0f, half_size * 2.0f);
            return proj * view;
        }
    }

    void Renderer3DShadow::execute_dir_draw(FrameGraphPassContext& ctx) {
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* data   = Renderer3DInternal::g_renderer3d_data;
        auto* vk_ctx = s_res->vk_ctx;
        const uint32_t frame = ctx.frame_index() % VulkanContext::k_max_frames_in_flight;

        const auto& dir_light = data->scene_lights.directional_light;
        const bool shadows_on = data->directional_shadows_enabled &&
                                dir_light.intensity > 0.0f;

        if (!s_dir)
            s_dir = new DirShadowResources{};

        // Register the shadow map's 2D view and comparison sampler with VulkanContext so
        // update_gbuffer_descriptors can write them to set=1 binding=5 every frame.
        if (!s_dir->resources_registered) {
            VkImageView  map_view    = ctx.get_resource_depth_sampler_image_view("shadowDirMap");
            VkSampler    cmp_sampler = ctx.get_resource_depth_comparison_sampler("shadowDirMap");
            if (map_view && cmp_sampler) {
                vk_ctx->set_dir_shadow_resources(map_view, cmp_sampler);
                s_dir->resources_registered = true;
            }
        }

        // Upload SSBO so the lighting shader always has valid data.
        {
            DirectionalShadowSSBO ssbo{};
            ssbo.enabled         = shadows_on ? 1u : 0u;
            ssbo.shadow_distance = data->directional_shadow_distance;
            if (shadows_on) {
                ssbo.light_view_proj = compute_dir_shadow_vp(
                    glm::vec3(dir_light.direction),
                    data->scene_camera_pos,
                    data->directional_shadow_distance);
            }
            vk_ctx->upload_directional_shadows(frame, ssbo);
        }

        // On the very first frame, transition to SHADER_READ so the lighting pass can safely
        // sample the map even when no shadow geometry is rendered yet.
        if (s_dir->first_frame) {
            VkImage image = ctx.get_resource_vk_image("shadowDirMap");
            ctx.submit_vulkan_graphics_raw([image](VkCommandBuffer cmd) {
                VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                imb.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
                imb.newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                imb.image            = image;
                imb.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
                imb.srcAccessMask    = 0;
                imb.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &imb);
            });
            s_dir->first_frame = false;
        }

        if (!shadows_on) return;
        if (data->shadow_draw_list.empty()) return;

        // Lazily create the pipeline against the shadowDirMap render pass.
        if (!s_dir->pipeline) {
            VkRenderPass rp = ctx.get_resource_render_pass("shadowDirMap");
            if (!rp) {
                HN_CORE_WARN("Renderer3DShadow: dir shadow render pass not available, skipping");
                return;
            }

            void* meshlet_extra_layout = VulkanRendererAPI::get_or_create_meshlet_set_layout();

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_ShadowDirectional.glsl");
            spec.pipelineKind = PipelineKind::MeshShading;
            spec.perColorAttachmentBlend.clear();
            spec.depthStencil.depthTest  = true;
            spec.depthStencil.depthWrite = true;
            spec.cullMode                = CullMode::Front;
            spec.depthBiasConstantFactor = 2.0f;
            spec.depthBiasSlopeFactor    = 1.5f;

            Ref<Pipeline> pipe = Pipeline::create(spec, rp, meshlet_extra_layout);
            HN_CORE_ASSERT(pipe, "Renderer3DShadow: failed to create directional shadow pipeline");

            s_dir->pipeline_ref = pipe;
            s_dir->pipeline     = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
            s_dir->layout       = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
        }

        if (!s_dir->pipeline) return;

        VkRenderPass  rp    = ctx.get_resource_render_pass("shadowDirMap");
        VkImage       image = ctx.get_resource_vk_image("shadowDirMap");
        VkFramebuffer fb    = ctx.get_resource_layer_framebuffer("shadowDirMap", 0);
        if (!rp || !fb) return;

        const uint32_t res_size = k_dir_shadow_map_resolution;

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            imb.oldLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            imb.newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
            imb.image            = image;
            imb.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            imb.srcAccessMask    = VK_ACCESS_SHADER_READ_BIT;
            imb.dstAccessMask    = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                 | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                0, 0, nullptr, 0, nullptr, 1, &imb);

            VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
            rpbi.renderPass        = rp;
            rpbi.framebuffer       = fb;
            rpbi.renderArea.extent = {res_size, res_size};
            VkClearValue cv{};
            cv.depthStencil        = {1.0f, 0};
            rpbi.clearValueCount   = 1;
            rpbi.pClearValues      = &cv;

            vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

            const VkViewport vp{0.0f, 0.0f, (float)res_size, (float)res_size, 0.0f, 1.0f};
            const VkRect2D   sc{{0, 0}, {res_size, res_size}};
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_dir->pipeline);

            VkDescriptorSet global_ds = vk_ctx->get_global_descriptor_set(frame);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                s_dir->layout, 0, 1, &global_ds, 0, nullptr);

            for (const auto& draw : data->shadow_draw_list) {
                VkDescriptorSet mesh_ds = reinterpret_cast<VkDescriptorSet>(draw.mesh_descriptor_set);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    s_dir->layout, 1, 1, &mesh_ds, 0, nullptr);

                // light_index and face_index are unused by the directional shader — pass zeros.
                ShadowDrawPC pc{draw.draw_data_base, 0, 0, 0};
                vkCmdPushConstants(cmd, s_dir->layout,
                    VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(ShadowDrawPC), &pc);

                s_fn_draw_mesh_tasks(cmd, draw.meshlet_count, 1, 1);
            }

            vkCmdEndRenderPass(cmd);
            // No explicit post-pass barrier needed: the render pass finalLayout is
            // DEPTH_STENCIL_READ_ONLY_OPTIMAL (depth_samplable=true) and the external
            // subpass dependency already covers DEPTH_WRITE -> SHADER_READ.
        });
    }

    void Renderer3DShadow::invalidate_dir_shadow_resources() {
        if (!s_dir) return;
        s_dir->resources_registered = false;
        s_dir->first_frame = true;
        s_dir->pipeline_ref.reset();
        s_dir->pipeline = VK_NULL_HANDLE;
        s_dir->layout   = VK_NULL_HANDLE;
        if (s_res && s_res->vk_ctx)
            s_res->vk_ctx->set_dir_shadow_resources(VK_NULL_HANDLE, VK_NULL_HANDLE);
    }

    void Renderer3DShadow::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();
        registry.register_executor("shadow.draw", [](FrameGraphPassContext& ctx) {
            Renderer3DShadow::execute_draw(ctx);
        });

        registry.register_executor("shadow.draw_directional", [](FrameGraphPassContext& ctx) {
            Renderer3DShadow::execute_dir_draw(ctx);
        });
    }

} // namespace Honey