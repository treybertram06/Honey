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

#include "Honey/core/settings.h"
#include "platform/vulkan/vk_gpu_profiler.h"

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {
        struct ShadowResources {
            VulkanContext* vk_ctx = nullptr;

            // Default, mesh shader path
            Ref<Pipeline>  shadow_pipeline_ref; // keeps VkPipeline alive
            VkPipeline     shadow_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout shadow_layout = VK_NULL_HANDLE;
            // Classic geo path
            Ref<Pipeline>  classic_pipeline_ref; // keeps VkPipeline alive
            VkPipeline     classic_pipeline = VK_NULL_HANDLE;
            VkPipelineLayout classic_layout = VK_NULL_HANDLE;

            bool           cubemap_first_frame = true;
            bool           shadow_resources_registered = false;

            uint32_t shadow_light_count = 0;
            ShadowMatricesSSBO ssbo_data{};
        };

        static ShadowResources* s_res = nullptr;

        static PFN_vkCmdDrawMeshTasksEXT         s_fn_draw_mesh_tasks          = nullptr;
        static PFN_vkCmdDrawMeshTasksIndirectEXT s_fn_draw_mesh_tasks_indirect = nullptr;
        static PFN_vkCmdDrawIndexed              s_fn_draw_indexed             = nullptr;
        static PFN_vkCmdDrawIndexedIndirect      s_fn_draw_indexed_indirect    = nullptr;

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

        s_fn_draw_mesh_tasks_indirect = reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>(
            vkGetDeviceProcAddr(ctx->get_device(), "vkCmdDrawMeshTasksIndirectEXT"));
        HN_CORE_ASSERT(s_fn_draw_mesh_tasks_indirect,
            "Renderer3DShadow: vkCmdDrawMeshTasksIndirectEXT not available");

        s_fn_draw_indexed = reinterpret_cast<PFN_vkCmdDrawIndexed>(
            vkGetDeviceProcAddr(ctx->get_device(), "vkCmdDrawIndexed"));
        s_fn_draw_indexed_indirect = reinterpret_cast<PFN_vkCmdDrawIndexedIndirect>(
            vkGetDeviceProcAddr(ctx->get_device(), "vkCmdDrawIndexedIndirect"));
    }

    void Renderer3DShadow::shutdown() {
        if (!s_res) return;

        delete s_res;
        s_res = nullptr;
        s_fn_draw_mesh_tasks          = nullptr;
        s_fn_draw_mesh_tasks_indirect = nullptr;
        s_fn_draw_indexed             = nullptr;
        s_fn_draw_indexed_indirect    = nullptr;
    }

    bool Renderer3DShadow::is_initialized() { return s_res != nullptr; }

    void Renderer3DShadow::execute_draw(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* res = s_res;
        const uint32_t frame = res->vk_ctx->get_current_frame() % VulkanContext::k_max_frames_in_flight;

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
        {
            HN_PROFILE_SCOPE("ShadowDraw::prepare_shadow_data");
            prepare_shadow_data(frame, Renderer3DInternal::g_renderer3d_data->scene_lights);
        }

        // On the very first frame, transition all cubemap layers to SHADER_READ so the deferred
        // lighting pass can safely sample them even when no shadow lights exist yet.
        if (res->cubemap_first_frame) {
            VkImage image = ctx.get_resource_vk_image("shadowCubemap");
            VkCommandBuffer cmd = ctx.cmd();
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
            res->cubemap_first_frame = false;
        }

        if (res->shadow_light_count == 0) return;

        // Lazily create the shadow pipeline on the first draw.
        const bool using_meshlet_path = Settings::get().renderer.geometry_path == GeometryPath::Meshlet;
        if (using_meshlet_path) {
            if (!res->shadow_pipeline) { HN_PROFILE_SCOPE("ShadowDraw::pipeline_create");
                VkRenderPass shadow_rp = ctx.get_resource_render_pass("shadowCubemap");
                if (!shadow_rp) {
                    HN_CORE_WARN("Renderer3DShadow: shadow render pass not available, skipping");
                    return;
                }

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
                // This pipeline pushes ShadowMeshletPushData, not the default PassPushData —
                // anchor the push-block validation in VulkanPipeline::create_mesh to the struct
                // it actually uses (see push_meshlet pushes at :330 below).
                spec.expected_push_constant_size = sizeof(ShadowMeshletPushData);

                Ref<Pipeline> pipe = Pipeline::create_heap_mode(spec, shadow_rp);
                HN_CORE_ASSERT(pipe, "Renderer3DShadow: failed to create shadow pipeline");

                res->shadow_pipeline_ref = pipe;  // keeps VkPipeline alive past this scope
                res->shadow_pipeline = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
                res->shadow_layout   = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
            }

            if (!res->shadow_pipeline) return;
        } else {
            if (!res->classic_pipeline) { HN_PROFILE_SCOPE("ShadowDraw::pipeline_create_classic");
                VkRenderPass shadow_rp = ctx.get_resource_render_pass("shadowCubemap");
                if (!shadow_rp) {
                    HN_CORE_WARN("Renderer3DShadow: shadow render pass not available, skipping");
                    return;
                }

                auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_ShadowCubemap_Classic.glsl");
                spec.pipelineKind = PipelineKind::Graphics;
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

                Ref<Pipeline> pipe = Pipeline::create(spec, shadow_rp, nullptr);
                HN_CORE_ASSERT(pipe, "Renderer3DShadow: failed to create classic shadow pipeline");

                res->classic_pipeline_ref = pipe;  // keeps VkPipeline alive past this scope
                res->classic_pipeline = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
                res->classic_layout   = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
            }

            if (!res->classic_pipeline) return;
        }
        if (using_meshlet_path && Renderer3DInternal::g_renderer3d_data->shadow_draw_list.empty()) return;
        if (!using_meshlet_path && Renderer3DInternal::g_renderer3d_data->classic_shadow_draw_list.empty()) return;

        if (using_meshlet_path && !Renderer3DInternal::g_renderer3d_data->indirect_buffers[frame]) return;

        VkRenderPass rp    = ctx.get_resource_render_pass("shadowCubemap");
        VkImage      image = ctx.get_resource_vk_image("shadowCubemap");

        const uint32_t res_size = k_shadow_map_resolution;

        {
            HN_PROFILE_SCOPE("ShadowDraw::record_commands");
            VkCommandBuffer cmd = ctx.cmd();
            HN_GPU_SCOPE(cmd, "Shadow: Point Lights");

            {
                HN_PROFILE_SCOPE("ShadowDraw::barrier_to_attachment");
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
            }

            const VkPipeline       active_pipeline = using_meshlet_path ? res->shadow_pipeline  : res->classic_pipeline;
            const VkPipelineLayout active_layout   = using_meshlet_path ? res->shadow_layout     : res->classic_layout;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline);

            auto* heap = res->vk_ctx->get_backend()->get_descriptor_heap();
            if (using_meshlet_path) {
                // Heap-mode pipeline: set=0 (shadow matrices, binding 6) resolves through the heap
                // globals registry, no bound descriptor set. Re-bind the heap since the GBuffer
                // pass earlier this frame used classic vkCmdBindDescriptorSets, which invalidates it.
                heap->bind(cmd);
            } else {
                // Bind the global descriptor set at set=0 — the shadow matrices SSBO is at binding 6.
                VkDescriptorSet global_ds = res->vk_ctx->get_global_descriptor_set(frame);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    active_layout, 0, 1, &global_ds, 0, nullptr);
            }

            const VkViewport vp{0.0f, 0.0f, (float)res_size, (float)res_size, 0.0f, 1.0f};
            const VkRect2D   sc{{0, 0}, {res_size, res_size}};

            VkBuffer indirect_vk = reinterpret_cast<VkBuffer>(
                Renderer3DInternal::g_renderer3d_data->indirect_buffers[frame]->get_native_buffer());

            {
                HN_PROFILE_SCOPE("ShadowDraw::face_draw_loop");
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

                        if (using_meshlet_path) {
                            HN_PROFILE_SCOPE("ShadowDraw::mesh_draw_calls");
                            for (const auto& group : Renderer3DInternal::g_renderer3d_data->shadow_draw_list) {
                                ShadowMeshletPushData pc{
                                    group.mesh_block_offset, (int32_t)group.draw_data_base, (int32_t)li, fi };
                                heap->push_pass_data(cmd, &pc, sizeof(pc));

                                s_fn_draw_mesh_tasks_indirect(cmd, indirect_vk,
                                    group.indirect_byte_offset, group.draw_count, 12u);
                            }
                        } else {
                            HN_PROFILE_SCOPE("ShadowDraw::classic_draw_calls");
                            VkBuffer inst_vk = reinterpret_cast<VkBuffer>(
                                Renderer3DInternal::g_renderer3d_data->instance_vb->get_native_buffer());
                            for (const auto& entry : Renderer3DInternal::g_renderer3d_data->classic_shadow_draw_list) {
                                const VkDeviceSize offset = 0;
                                vkCmdBindVertexBuffers(cmd, 0, 1, &entry.vertex_buffer, &offset);
                                vkCmdBindVertexBuffers(cmd, 1, 1, &inst_vk, &offset);
                                vkCmdBindIndexBuffer(cmd, entry.index_buffer, 0, VK_INDEX_TYPE_UINT32);

                                ShadowDrawPC pc{entry.draw_data_base, (int32_t)li, fi, 0};
                                vkCmdPushConstants(cmd, active_layout,
                                    VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                    0, sizeof(ShadowDrawPC), &pc);

                                s_fn_draw_indexed(cmd, entry.index_count, entry.draw_count, 0, 0, entry.draw_data_base);
                            }
                        }

                        vkCmdEndRenderPass(cmd);
                    }
                }
            }

            {
                HN_PROFILE_SCOPE("ShadowDraw::barrier_to_shader_read");
                // Transition all layers back to SHADER_READ for the deferred lighting pass.
                VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                imb.image            = image;
                imb.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, k_max_shadow_lights * 6 };
                imb.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                imb.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                imb.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &imb);
            }
        }
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
        s_res->classic_pipeline_ref.reset();
        s_res->classic_pipeline = VK_NULL_HANDLE;
        s_res->classic_layout   = VK_NULL_HANDLE;
        // Clear stale handles in VulkanContext before the old framebuffer is destroyed.
        if (s_res->vk_ctx)
            s_res->vk_ctx->set_shadow_cubemap_resources(VK_NULL_HANDLE, VK_NULL_HANDLE);
    }

    // -------------------------------------------------------------------------
    // Directional light shadow map
    // -------------------------------------------------------------------------

    namespace {
        struct DirShadowResources {
            // Default, mesh path
            Ref<Pipeline> pipeline_ref;
            VkPipeline    pipeline    = VK_NULL_HANDLE;
            VkPipelineLayout layout   = VK_NULL_HANDLE;
            // Classic geo
            Ref<Pipeline> classic_pipeline_ref;
            VkPipeline    classic_pipeline    = VK_NULL_HANDLE;
            VkPipelineLayout classic_layout   = VK_NULL_HANDLE;

            bool resources_registered = false;
            bool first_frame          = true;
        };

        static DirShadowResources* s_dir = nullptr;

        static glm::mat4 compute_cascade_vp(
            glm::vec3        light_dir,
            glm::vec3        cam_pos,
            float            cam_far,
            const glm::mat4& inv_view_proj,
            float            sub_near,
            float            sub_far) {
            const glm::vec4 far_ndc[4] = {
                {-1.f, -1.f, 1.f, 1.f},
                { 1.f, -1.f, 1.f, 1.f},
                { 1.f,  1.f, 1.f, 1.f},
                {-1.f,  1.f, 1.f, 1.f},
            };
            glm::vec3 far_world[4];
            for (int i = 0; i < 4; ++i) {
                glm::vec4 w = inv_view_proj * far_ndc[i];
                far_world[i] = glm::vec3(w) / w.w;
            }

            glm::vec3 corners[8];
            for (int i = 0; i < 4; ++i) {
                glm::vec3 ray = far_world[i] - cam_pos;  // length ≈ cam_far
                corners[i]     = cam_pos + ray * (sub_near / cam_far);  // near face
                corners[i + 4] = cam_pos + ray * (sub_far  / cam_far);  // far face
            }

            glm::vec3 L  = glm::normalize(-light_dir);
            glm::vec3 up = (glm::abs(L.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);

            glm::vec3 centroid{0};
            for (auto& c : corners) centroid += c;
            centroid /= 8.f;

            // Build light-space basis vectors
            glm::vec3 forward = -L;
            glm::vec3 right   = glm::normalize(glm::cross(up, forward));
            glm::vec3 up_     = glm::cross(forward, right);

            // Bounding sphere radius = max distance from centroid to any corner.
            // Computing from the actual corners (rather than analytically from fov/aspect)
            // guarantees correctness for any viewport shape and avoids stale camera parameters.
            // Euclidean distance is rotation-invariant, so this is shimmer-safe.
            float radius = 0.0f;
            for (auto& c : corners)
                radius = glm::max(radius, glm::length(c - centroid));

            // Stable texel size — same bits every frame
            const float res         = float(k_dir_shadow_map_resolution);
            const float texel_world = (2.f * radius) / res;

            // Round radius up to a texel boundary so the ortho extents are also stable.
            // Add one extra texel of slack: floor-snapping the centroid (below) can shift
            // it by up to one texel in the negative direction, which would push +right/+up_
            // frustum corners (at exactly radius from the true centroid) just outside the
            // ortho box. The extra texel absorbs that worst-case offset.
            radius = glm::ceil(radius / texel_world) * texel_world + texel_world;

            // Snap centroid to texel grid using the now-stable texel_world
            float cx = glm::dot(centroid, right);
            float cy = glm::dot(centroid, up_);
            cx = glm::floor(cx / texel_world) * texel_world;
            cy = glm::floor(cy / texel_world) * texel_world;

            glm::vec3 snapped_centroid = cx * right + cy * up_;
            snapped_centroid += glm::dot(centroid, forward) * forward;

            constexpr float k_near_pull = 200.f;
            glm::mat4 light_view = glm::lookAt(
                snapped_centroid + L * k_near_pull,
                snapped_centroid,
                up_);

            // Use sphere diameter for z_far — rotation-invariant (AABB depth along the light
            // direction changes with camera rotation, but sphere diameter is constant).
            float z_far = k_near_pull + 2.0f * radius;
            glm::mat4 proj = glm::orthoRH_ZO(
                -radius,  radius,
                -radius,  radius,
                0.001f,   z_far);

            return proj * light_view;
        }

        static void compute_csm_cascades(
            glm::vec3        light_dir,
            glm::vec3        cam_pos,
            float            cam_near,
            float            cam_far,
            float            shadow_far,
            float            lambda,
            const glm::mat4& inv_view_proj,
            uint32_t         count,
            glm::mat4*       out_vp,      // [count]
            float*           out_splits)  // [count] view-space far-plane per cascade
        {
            float eff_far = glm::min(cam_far, shadow_far);

            for (uint32_t i = 0; i < count; ++i) {
                float p     = float(i + 1) / float(count);
                float log_s = cam_near * glm::pow(eff_far / cam_near, p);
                float uni_s = cam_near + (eff_far - cam_near) * p;
                float split = lambda * log_s + (1.f - lambda) * uni_s;
                out_splits[i] = split;

                float sub_near = (i == 0) ? cam_near : out_splits[i - 1];
                float sub_far  = split;

                out_vp[i] = compute_cascade_vp(
                    light_dir, cam_pos, cam_far, inv_view_proj, sub_near, sub_far);
            }
        }
    }

    void Renderer3DShadow::execute_dir_draw(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* data   = Renderer3DInternal::g_renderer3d_data;
        auto* vk_ctx = s_res->vk_ctx;
        const uint32_t frame = vk_ctx->get_current_frame() % VulkanContext::k_max_frames_in_flight;

        const auto& dir_light = data->scene_lights.directional_light;
        const bool shadows_on = data->directional_shadows_enabled &&
                                dir_light.intensity > 0.0f;

        if (!s_dir)
            s_dir = new DirShadowResources{};

        // Register the shadow map's 2D view and comparison sampler with VulkanContext for set=0 binding=9 (forward pass).
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
            HN_PROFILE_SCOPE("DirShadow::compute_and_upload_ssbo");
            DirectionalShadowSSBO ssbo{};
            ssbo.enabled         = shadows_on ? 1u : 0u;
            ssbo.shadow_distance = data->directional_shadow_distance;
            ssbo.cascade_count   = k_csm_cascade_count;

            if (shadows_on) {
                HN_PROFILE_SCOPE("DirShadow::compute_csm_cascades");
                compute_csm_cascades(
                    glm::vec3(dir_light.direction),
                    data->scene_camera_pos,
                    data->scene_camera_near,
                    data->scene_camera_far,
                    data->directional_shadow_distance,
                    0.75f,
                    glm::inverse(data->scene_view_proj),
                    k_csm_cascade_count,
                    ssbo.cascade_vp,
                    ssbo.cascade_splits);
            }
            {
                HN_PROFILE_SCOPE("DirShadow::upload_ssbo");
                vk_ctx->upload_directional_shadows(frame, ssbo);
            }
        }


        // On the very first frame, transition to SHADER_READ so the lighting pass can safely
        // sample the map even when no shadow geometry is rendered yet.
        if (s_dir->first_frame) {
            VkImage image = ctx.get_resource_vk_image("shadowDirMap");
            VkCommandBuffer cmd = ctx.cmd();
            VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
            imb.oldLayout        = VK_IMAGE_LAYOUT_UNDEFINED;
            imb.newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            imb.image            = image;
            imb.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, k_csm_cascade_count };
            imb.srcAccessMask    = 0;
            imb.dstAccessMask    = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 0, nullptr, 0, nullptr, 1, &imb);
            s_dir->first_frame = false;
        }

        if (!shadows_on) return;

        const bool meshlet_path = Settings::get().renderer.geometry_path == GeometryPath::Meshlet;
        if ( meshlet_path && data->shadow_draw_list.empty()) return;
        if (!meshlet_path && data->classic_shadow_draw_list.empty()) return;

        // Lazily create the pipeline against the shadowDirMap render pass.
        if (meshlet_path) {
            if (!s_dir->pipeline) { HN_PROFILE_SCOPE("DirShadow::pipeline_create");
                VkRenderPass rp = ctx.get_resource_render_pass("shadowDirMap");
                if (!rp) {
                    HN_CORE_WARN("Renderer3DShadow: dir shadow render pass not available, skipping");
                    return;
                }

                auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_ShadowDirectional.glsl");
                spec.pipelineKind = PipelineKind::MeshShading;
                spec.perColorAttachmentBlend.clear();
                spec.depthStencil.depthTest  = true;
                spec.depthStencil.depthWrite = true;
                spec.cullMode                = CullMode::Front;
                spec.depthBiasConstantFactor = 2.0f;
                spec.depthBiasSlopeFactor    = 1.5f;
                // See the cubemap shadow pipeline above: this pass also pushes ShadowMeshletPushData.
                spec.expected_push_constant_size = sizeof(ShadowMeshletPushData);

                Ref<Pipeline> pipe = Pipeline::create_heap_mode(spec, rp);
                HN_CORE_ASSERT(pipe, "Renderer3DShadow: failed to create directional shadow pipeline");

                s_dir->pipeline_ref = pipe;
                s_dir->pipeline     = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
                s_dir->layout       = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
            }
            if (!s_dir->pipeline) return;
            if (!data->indirect_buffers[frame]) return;
        } else {
            if (!s_dir->classic_pipeline) { HN_PROFILE_SCOPE("DirShadow::pipeline_create_classic");
                VkRenderPass rp = ctx.get_resource_render_pass("shadowDirMap");
                if (!rp) {
                    HN_CORE_WARN("Renderer3DShadow: dir shadow render pass not available, skipping");
                    return;
                }

                auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_ShadowDirectional_Classic.glsl");
                spec.pipelineKind = PipelineKind::Graphics;
                spec.perColorAttachmentBlend.clear();
                spec.depthStencil.depthTest  = true;
                spec.depthStencil.depthWrite = true;
                spec.cullMode                = CullMode::Front;
                spec.depthBiasConstantFactor = 2.0f;
                spec.depthBiasSlopeFactor    = 1.5f;

                Ref<Pipeline> pipe = Pipeline::create(spec, rp, nullptr);
                HN_CORE_ASSERT(pipe, "Renderer3DShadow: failed to create classic directional shadow pipeline");

                s_dir->classic_pipeline_ref = pipe;
                s_dir->classic_pipeline     = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
                s_dir->classic_layout       = reinterpret_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());
            }
            if (!s_dir->classic_pipeline) return;
        }

        VkRenderPass  rp    = ctx.get_resource_render_pass("shadowDirMap");
        VkImage       image = ctx.get_resource_vk_image("shadowDirMap");
        if (!rp) return;

        const uint32_t res_size = k_dir_shadow_map_resolution;

        {
            HN_PROFILE_SCOPE("DirShadow::record_commands");
            VkCommandBuffer cmd = ctx.cmd();
            HN_GPU_SCOPE(cmd, "Shadow: Dir Lights");

            {
                HN_PROFILE_SCOPE("DirShadow::barrier_to_attachment");
                // Transition all cascade layers: SHADER_READ → DEPTH_ATTACHMENT
                VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                imb.oldLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                imb.newLayout        = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                imb.image            = image;
                imb.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, k_csm_cascade_count };
                imb.srcAccessMask    = VK_ACCESS_SHADER_READ_BIT;
                imb.dstAccessMask    = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
                                     | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &imb);
            }

            const VkPipeline       active_pipeline = meshlet_path ? s_dir->pipeline        : s_dir->classic_pipeline;
            const VkPipelineLayout active_layout   = meshlet_path ? s_dir->layout           : s_dir->classic_layout;

            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, active_pipeline);

            auto* heap = vk_ctx->get_backend()->get_descriptor_heap();
            if (meshlet_path) {
                // Heap-mode pipeline: set=0 (shadow matrices) resolves through the heap globals
                // registry. Re-bind since the GBuffer pass's classic descriptor-set binds earlier
                // this frame invalidate heap binding state.
                heap->bind(cmd);
            } else {
                VkDescriptorSet global_ds = vk_ctx->get_global_descriptor_set(frame);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    active_layout, 0, 1, &global_ds, 0, nullptr);
            }

            const VkViewport viewport{0.f, 0.f, (float)res_size, (float)res_size, 0.f, 1.f};
            const VkRect2D   scissor{{0, 0}, {res_size, res_size}};

            VkBuffer indirect_vk = meshlet_path
                ? reinterpret_cast<VkBuffer>(data->indirect_buffers[frame]->get_native_buffer())
                : VK_NULL_HANDLE;

            {
                HN_PROFILE_SCOPE("DirShadow::cascade_draw_loop");
                // Render one pass per cascade layer. face_index in the push data carries
                // the cascade index so the shader looks up cascade_vp[face_index].
                for (uint32_t c = 0; c < k_csm_cascade_count; ++c) {
                    VkFramebuffer fb = ctx.get_resource_layer_framebuffer("shadowDirMap", c);
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
                    vkCmdSetViewport(cmd, 0, 1, &viewport);
                    vkCmdSetScissor(cmd,  0, 1, &scissor);

                    if (meshlet_path) {
                        HN_PROFILE_SCOPE("DirShadow::mesh_draw_calls");
                        for (const auto& group : data->shadow_draw_list) {
                            ShadowMeshletPushData pc{
                                group.mesh_block_offset, (int32_t)group.draw_data_base, 0, c };
                            heap->push_pass_data(cmd, &pc, sizeof(pc));

                            s_fn_draw_mesh_tasks_indirect(cmd, indirect_vk,
                                group.indirect_byte_offset, group.draw_count, 12u);
                        }
                    } else {
                        HN_PROFILE_SCOPE("DirShadow::classic_draw_calls");
                        for (const auto& entry : data->classic_shadow_draw_list) {
                            const VkDeviceSize vb_offset = 0;
                            vkCmdBindVertexBuffers(cmd, 0, 1, &entry.vertex_buffer, &vb_offset);
                            vkCmdBindIndexBuffer(cmd, entry.index_buffer, 0, VK_INDEX_TYPE_UINT32);

                            ShadowDrawPC pc{entry.draw_data_base, 0, c, 0};
                            vkCmdPushConstants(cmd, active_layout,
                                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                0, sizeof(ShadowDrawPC), &pc);

                            s_fn_draw_indexed(cmd, entry.index_count, entry.draw_count, 0, 0, entry.draw_data_base);
                        }
                    }

                    vkCmdEndRenderPass(cmd);
                }
            }

            {
                HN_PROFILE_SCOPE("DirShadow::barrier_to_shader_read");
                // Transition all layers back: DEPTH_ATTACHMENT → SHADER_READ
                VkImageMemoryBarrier imb{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                imb.image            = image;
                imb.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, k_csm_cascade_count };
                imb.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
                imb.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
                imb.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
                imb.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &imb);
            }
        }
    }

    void Renderer3DShadow::invalidate_dir_shadow_resources() {
        if (!s_dir) return;
        s_dir->resources_registered = false;
        s_dir->first_frame = true;
        s_dir->pipeline_ref.reset();
        s_dir->pipeline = VK_NULL_HANDLE;
        s_dir->layout   = VK_NULL_HANDLE;
        s_dir->classic_pipeline_ref.reset();
        s_dir->classic_pipeline = VK_NULL_HANDLE;
        s_dir->classic_layout   = VK_NULL_HANDLE;
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