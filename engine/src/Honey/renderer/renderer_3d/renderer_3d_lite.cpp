#include "hnpch.h"
#include "renderer_3d_lite.h"

#include <algorithm>

#include "Honey/core/engine.h"
#include "Honey/renderer/texture.h"
#include "platform/vulkan/vk_backend.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_texture.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    namespace {
        using Renderer3DInternal::MeshletDrawCommand;
        using Renderer3DInternal::PipelineVariantKey;
        using Renderer3DInternal::PipelineVariantKeyHash;

        constexpr uint32_t k_frames = VulkanContext::k_max_frames_in_flight;
        constexpr uint32_t k_max_sets_per_frame = 2048;

        // std140, matches LiteFrame in Renderer3D_Lite.glsl
        struct LiteFrameUBO {
            glm::mat4 view_proj;
            glm::vec4 cam_pos;
            glm::vec4 light_dir;   // xyz = direction the light travels, w = intensity
            glm::vec4 light_color;
            glm::vec4 ambient;
            glm::vec4 viewport;    // xy = render target size in pixels
        };

        // Matches DebugVertex in debug_renderer_3d.cpp (shader reads it as 7 floats)
        constexpr uint32_t k_debug_vertex_size = 28;

        // Matches PC in Renderer3D_Lite.glsl
        struct LitePush {
            glm::mat4 model;
            glm::vec4 base_color;
            uint32_t  first_index;
            int32_t   entity_id;
            uint32_t  mode;
            uint32_t  flags;
        };
        static_assert(sizeof(LitePush) == 96, "LitePush layout mismatch");

        struct LiteRendererResources {
            VulkanContext* vk_ctx = nullptr;
            VkDevice device = VK_NULL_HANDLE;

            VkDescriptorSetLayout set_layouts[3]{}; // 0 frame UBO, 1 mesh SSBOs, 2 base-colour texture
            VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
            VkDescriptorPool pools[k_frames]{};
            Ref<StorageBuffer> frame_ubos[k_frames]{};
            Ref<Texture2D> white_texture;

            std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> pipelines;
            std::unordered_map<void*, Ref<Pipeline>> line_pipelines;
            std::unordered_map<void*, Ref<Pipeline>> icon_pipelines;
            bool warned_pool_exhausted = false;

            // Overlay intake for this frame; consumed and cleared by record().
            std::vector<uint8_t> debug_lines;
            Ref<StorageBuffer> line_buffers[k_frames]{};
        };
        static LiteRendererResources* s_res = nullptr;

        VkDescriptorSetLayout make_set_layout(VkDevice device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings) {
            VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            ci.bindingCount = (uint32_t)bindings.size();
            ci.pBindings = bindings.begin();
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            VkResult r = vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout);
            HN_CORE_ASSERT(r == VK_SUCCESS, "Renderer3DLite: vkCreateDescriptorSetLayout failed");
            return layout;
        }

        Ref<Pipeline> get_or_create_pipeline(void* rp_native, uint32_t color_attachment_count, bool blend, bool cull_none) {
            PipelineVariantKey key{rp_native, (uint8_t)(blend ? 1 : 0), (uint8_t)(cull_none ? 1 : 0)};
            auto it = s_res->pipelines.find(key);
            if (it != s_res->pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_Lite.glsl");
            // Keep from_shader's cull mode (the settings value, same as the full tier) unless the
            // material needs none.
            if (cull_none)
                spec.cullMode = CullMode::None;
            spec.depthStencil.depthTest = true;
            spec.depthStencil.depthWrite = !blend;
            // Match the render pass exactly. Only attachment 0 (colour) blends; the entity-id
            // attachment (R32_SINT) cannot blend.
            spec.perColorAttachmentBlend.assign(color_attachment_count, AttachmentBlendState{});
            spec.perColorAttachmentBlend[0].enabled = blend;

            HN_CORE_INFO("Renderer3DLite: creating pipeline (blend={0}, cull_none={1}, color attachments={2})", blend, cull_none, color_attachment_count);
            auto pipeline = Pipeline::create_layout_mode(spec, rp_native, s_res->pipeline_layout);
            s_res->pipelines.emplace(key, pipeline);
            return pipeline;
        }

        // Lines / icons: alpha blend on attachment 0, never writes depth, no culling.
        Ref<Pipeline> get_or_create_overlay_pipeline(std::unordered_map<void*, Ref<Pipeline>>& cache, void* rp_native,
                                                     uint32_t color_attachment_count, PrimitiveTopology topology, bool depth_test) {
            auto it = cache.find(rp_native);
            if (it != cache.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_Lite.glsl");
            spec.topology = topology;
            spec.cullMode = CullMode::None;
            spec.depthStencil.depthTest = depth_test;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.assign(color_attachment_count, AttachmentBlendState{});
            spec.perColorAttachmentBlend[0].enabled = true;

            auto pipeline = Pipeline::create_layout_mode(spec, rp_native, s_res->pipeline_layout);
            cache.emplace(rp_native, pipeline);
            return pipeline;
        }
    }

    void Renderer3DLite::init(VulkanContext *ctx) {
        if (s_res) return;
        s_res = new LiteRendererResources{};
        s_res->vk_ctx = ctx;
        VkDevice device = s_res->device = ctx->get_device();

        s_res->set_layouts[0] = make_set_layout(device, {
            {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
        });
        s_res->set_layouts[1] = make_set_layout(device, {
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr}
        });
        s_res->set_layouts[2] = make_set_layout(device, {
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
        });

        VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LitePush)};
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        plci.setLayoutCount = 3;
        plci.pSetLayouts = s_res->set_layouts;
        plci.pushConstantRangeCount = 1;
        plci.pPushConstantRanges = &push;
        VkResult r = vkCreatePipelineLayout(device, &plci, nullptr, &s_res->pipeline_layout);
        HN_CORE_ASSERT(r == VK_SUCCESS, "Renderer3DLite: vkCreatePipelineLayout failed");

        VkDescriptorPoolSize sizes[] = {
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 16},
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, k_max_sets_per_frame * 2},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_max_sets_per_frame},
        };
        for (uint32_t i = 0; i < k_frames; ++i) {
            VkDescriptorPoolCreateInfo pci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            pci.maxSets = k_max_sets_per_frame;
            pci.poolSizeCount = 3;
            pci.pPoolSizes = sizes;
            r = vkCreateDescriptorPool(device, &pci, nullptr, &s_res->pools[i]);
            HN_CORE_ASSERT(r == VK_SUCCESS, "Renderer3DLite: vkCreateDescriptorPool failed");
            s_res->frame_ubos[i] = StorageBuffer::create(sizeof(LiteFrameUBO), StorageBufferUsage::Dynamic);
        }

        s_res->white_texture = Texture2D::create(1, 1);
        uint32_t white = 0xffffffff;
        s_res->white_texture->set_data(&white, sizeof(white));
    }

    void Renderer3DLite::shutdown() {
        if (!s_res) return;
        vkDeviceWaitIdle(s_res->device);
        // pipelines reference the layout; destroy them first
        s_res->pipelines.clear();
        s_res->line_pipelines.clear();
        s_res->icon_pipelines.clear();
        for (auto pool : s_res->pools)
            vkDestroyDescriptorPool(s_res->device, pool, nullptr);
        vkDestroyPipelineLayout(s_res->device, s_res->pipeline_layout, nullptr);
        for (auto layout : s_res->set_layouts)
            vkDestroyDescriptorSetLayout(s_res->device, layout, nullptr);
        delete s_res;
        s_res = nullptr;
    }

    bool Renderer3DLite::is_initialized() {
        return s_res != nullptr;
    }

    void Renderer3DLite::record(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res) return;

        auto* data = Renderer3DInternal::g_renderer3d_data;

        // Take this frame's debug lines; they are consumed whether or not anything gets drawn.
        std::vector<uint8_t> lines = std::move(s_res->debug_lines);
        s_res->debug_lines.clear();
        const uint32_t line_vertex_count = (uint32_t)(lines.size() / k_debug_vertex_size);

        if (data->meshlet_draws.empty() && line_vertex_count == 0 && data->icon_draws.empty()) return;

        auto target = ctx.get_pass_target_framebuffer();
        auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "Renderer3DLite::record: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();
        const uint32_t color_count = vk_fb->get_color_attachment_count();

        VkDevice device = s_res->device;
        const uint32_t slot = s_res->vk_ctx->get_current_frame() % k_frames;
        VkDescriptorPool pool = s_res->pools[slot];
        vkResetDescriptorPool(device, pool, 0); // this slot's previous frame has finished

        // Frame UBO. Directional light falls back to a fixed headlight-ish light if the scene has none.
        LiteFrameUBO frame{};
        // The camera matrices are GL-style; same GL -> Vulkan clip correction as make_gpu_camera() in
        // vk_renderer_globals.cpp (flip Y, remap z from [-1,1] to [0,1]).
        glm::mat4 clip_correction(1.0f);
        clip_correction[1][1] = -1.0f;
        clip_correction[2][2] = 0.5f;
        clip_correction[3][2] = 0.5f;
        frame.view_proj = clip_correction * data->scene_view_proj;
        frame.cam_pos = glm::vec4(data->scene_camera_pos, 1.0f);
        const auto& dl = data->scene_lights.directional_light;
        if (dl.intensity > 0.0f) {
            frame.light_dir = glm::vec4(dl.direction, dl.intensity);
            frame.light_color = glm::vec4(dl.color, 1.0f);
        } else {
            frame.light_dir = glm::vec4(glm::normalize(glm::vec3(-0.4f, -1.0f, -0.6f)), 1.0f);
            frame.light_color = glm::vec4(1.0f);
        }
        frame.ambient = glm::vec4(0.15f, 0.15f, 0.15f, 1.0f);
        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        frame.viewport = glm::vec4((float)ext.width, (float)ext.height, 0.0f, 0.0f);
        s_res->frame_ubos[slot]->set_data(&frame, sizeof(frame));

        // Line vertices as an SSBO (always at least a little, so set 1 is valid for icon draws too).
        auto& line_buffer = s_res->line_buffers[slot];
        const uint32_t line_bytes = std::max(line_vertex_count * k_debug_vertex_size, 256u);
        if (!line_buffer || line_buffer->get_size() < line_bytes)
            line_buffer = StorageBuffer::create(line_bytes, StorageBufferUsage::Dynamic);
        if (line_vertex_count > 0)
            line_buffer->set_data(lines.data(), line_vertex_count * k_debug_vertex_size);

        auto alloc_set = [&](VkDescriptorSetLayout layout) -> VkDescriptorSet {
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = pool;
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(device, &ai, &set) != VK_SUCCESS) {
                if (!s_res->warned_pool_exhausted) {
                    HN_CORE_WARN("Renderer3DLite: descriptor pool exhausted, skipping draws");
                    s_res->warned_pool_exhausted = true;
                }
                return VK_NULL_HANDLE;
            }
            return set;
        };

        VkDescriptorSet frame_set = alloc_set(s_res->set_layouts[0]);
        if (!frame_set) return;
        {
            VkDescriptorBufferInfo bi{reinterpret_cast<VkBuffer>(s_res->frame_ubos[slot]->get_native_buffer()), 0, sizeof(LiteFrameUBO)};
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = frame_set;
            w.descriptorCount = 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.pBufferInfo = &bi;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }

        // Opaque first (grouped by mesh to limit set rebinds), then blended back-to-front.
        std::vector<uint32_t> opaque, blended;
        for (uint32_t i = 0; i < (uint32_t)data->meshlet_draws.size(); ++i) {
            const auto& cmd = data->meshlet_draws[i];
            const bool blend = cmd.material && cmd.material->get_alpha_mode() == Material::AlphaMode::Blend;
            (blend ? blended : opaque).push_back(i);
        }
        std::stable_sort(opaque.begin(), opaque.end(), [&](uint32_t a, uint32_t b) {
            return data->meshlet_draws[a].mesh < data->meshlet_draws[b].mesh;
        });
        auto dist2 = [&](uint32_t i) {
            const glm::vec3 p(data->meshlet_draws[i].transform[3]);
            return glm::dot(p - data->scene_camera_pos, p - data->scene_camera_pos);
        };
        std::stable_sort(blended.begin(), blended.end(), [&](uint32_t a, uint32_t b) { return dist2(a) > dist2(b); });

        VkViewport vp{0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, {ext.width, ext.height}};

        VkSampler sampler = Application::get().get_vulkan_backend().get_sampler_linear();
        auto white_vk = std::dynamic_pointer_cast<VulkanTexture2D>(s_res->white_texture);

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "Lite Scene");
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_res->pipeline_layout, 0, 1, &frame_set, 0, nullptr);

            // Sets are cached per frame so repeated meshes/textures reuse them.
            std::unordered_map<const Mesh*, VkDescriptorSet> mesh_sets;
            std::unordered_map<void*, VkDescriptorSet> texture_sets;
            const Mesh* bound_mesh = nullptr;
            void* bound_view = nullptr;
            VkPipeline bound_pipeline = VK_NULL_HANDLE;

            // set 2: one descriptor set per texture view per frame
            auto texture_set_for = [&](void* view) -> VkDescriptorSet {
                auto tit = texture_sets.find(view);
                if (tit != texture_sets.end())
                    return tit->second;
                VkDescriptorSet set = alloc_set(s_res->set_layouts[2]);
                if (set) {
                    VkDescriptorImageInfo ii{sampler, reinterpret_cast<VkImageView>(view), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                    w.dstSet = set;
                    w.descriptorCount = 1;
                    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    w.pImageInfo = &ii;
                    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
                }
                texture_sets.emplace(view, set);
                return set;
            };

            auto draw =[&](uint32_t idx, bool blend) {
                const MeshletDrawCommand& dc = data->meshlet_draws[idx];
                if (!dc.mesh || !dc.mesh->meshlet_buffers || !dc.mesh->meshlet_buffers->flat_index_buffer) return;
                const auto& bufs = *dc.mesh->meshlet_buffers;

                // set 1: mesh vertex + flat index SSBOs
                auto mit = mesh_sets.find(dc.mesh);
                if (mit == mesh_sets.end()) {
                    VkDescriptorSet set = alloc_set(s_res->set_layouts[1]);
                    if (set) {
                        VkDescriptorBufferInfo bi[2] = {
                            {reinterpret_cast<VkBuffer>(bufs.vertex_buffer->get_native_buffer()), 0, VK_WHOLE_SIZE},
                            {reinterpret_cast<VkBuffer>(bufs.flat_index_buffer->get_native_buffer()), 0, VK_WHOLE_SIZE},
                        };
                        VkWriteDescriptorSet w[2]{};
                        for (uint32_t b = 0; b < 2; ++b) {
                            w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                            w[b].dstSet = set;
                            w[b].dstBinding = b;
                            w[b].descriptorCount = 1;
                            w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                            w[b].pBufferInfo = &bi[b];
                        }
                        vkUpdateDescriptorSets(device, 2, w, 0, nullptr);
                    }
                    mit = mesh_sets.emplace(dc.mesh, set).first;
                }
                if (!mit->second) return;

                // set 2: base-colour texture (1x1 white while missing or still uploading)
                std::shared_ptr<VulkanTexture2D> tex = dc.material
                    ? std::dynamic_pointer_cast<VulkanTexture2D>(dc.material->get_base_color_texture()) : nullptr;
                const bool has_tex = tex && tex->get_vk_image_view();
                if (!has_tex) tex = white_vk;
                void* view = tex->get_vk_image_view();
                VkDescriptorSet tex_set = texture_set_for(view);
                if (!tex_set) return;

                const bool cull_none = blend || (dc.material && dc.material->get_double_sided());
                VkPipeline pipe = reinterpret_cast<VkPipeline>(get_or_create_pipeline(rp_native, color_count, blend, cull_none)->get_native_pipeline());
                if (pipe != bound_pipeline) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
                    bound_pipeline = pipe;
                }
                if (dc.mesh != bound_mesh) {
                    VkDescriptorSet s = mit->second;
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_res->pipeline_layout, 1, 1, &s, 0, nullptr);
                    bound_mesh = dc.mesh;
                }
                if (view != bound_view) {
                    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_res->pipeline_layout, 2, 1, &tex_set, 0, nullptr);
                    bound_view = view;
                }

                const auto& geo = dc.submesh->meshlets;
                LitePush pc{};
                pc.model = dc.transform;
                pc.base_color = dc.material ? dc.material->get_base_color_factor() : glm::vec4(1.0f);
                pc.first_index = geo.flat_index_first * 3; // flat_index_first is in triangles
                pc.entity_id = dc.entity_id;
                pc.mode = 0;
                pc.flags = has_tex ? 1u : 0u;
                vkCmdPushConstants(cmd, s_res->pipeline_layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                vkCmdDraw(cmd, geo.flat_index_tri_count * 3, 1, 0, 0);
                data->stats.draw_calls++;
            };

            for (uint32_t i : opaque)  draw(i, false);
            for (uint32_t i : blended) draw(i, true);

            // Overlay (debug lines, then icons): shares set 1 (line buffer) and set 2 (white texture).
            if (line_vertex_count == 0 && data->icon_draws.empty())
                return;

            VkDescriptorSet overlay_set = alloc_set(s_res->set_layouts[1]);
            VkDescriptorSet white_set = texture_set_for(white_vk->get_vk_image_view());
            if (!overlay_set || !white_set)
                return;
            {
                VkDescriptorBufferInfo bi{reinterpret_cast<VkBuffer>(line_buffer->get_native_buffer()), 0, VK_WHOLE_SIZE};
                VkWriteDescriptorSet w[2]{};
                for (uint32_t b = 0; b < 2; ++b) {
                    w[b].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                    w[b].dstSet = overlay_set;
                    w[b].dstBinding = b;
                    w[b].descriptorCount = 1;
                    w[b].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                    w[b].pBufferInfo = &bi;
                }
                vkUpdateDescriptorSets(device, 2, w, 0, nullptr);
            }
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_res->pipeline_layout, 1, 1, &overlay_set, 0, nullptr);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_res->pipeline_layout, 2, 1, &white_set, 0, nullptr);

            constexpr VkShaderStageFlags k_push_stages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

            if (line_vertex_count > 0) {
                auto pipe = get_or_create_overlay_pipeline(s_res->line_pipelines, rp_native, color_count, PrimitiveTopology::Lines, false);
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<VkPipeline>(pipe->get_native_pipeline()));
                LitePush pc{};
                pc.mode = 1;
                pc.entity_id = -1;
                vkCmdPushConstants(cmd, s_res->pipeline_layout, k_push_stages, 0, sizeof(pc), &pc);
                vkCmdDraw(cmd, line_vertex_count, 1, 0, 0);
                data->stats.draw_calls++;
            }

            if (!data->icon_draws.empty()) {
                auto pipe = get_or_create_overlay_pipeline(s_res->icon_pipelines, rp_native, color_count, PrimitiveTopology::Triangles, true); // depth-tested at the icon centre's depth, so geometry occludes it
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<VkPipeline>(pipe->get_native_pipeline()));
                for (const auto& icon : data->icon_draws) {
                    // Flat tinted disc with a constant pixel size (world-space sizing is not supported in lite).
                    LitePush pc{};
                    pc.model[0] = icon.world_pos; // xyz = position, w = size in pixels
                    pc.base_color = icon.tint;
                    pc.entity_id = icon.entity_id;
                    pc.mode = 2;
                    vkCmdPushConstants(cmd, s_res->pipeline_layout, k_push_stages, 0, sizeof(pc), &pc);
                    vkCmdDraw(cmd, 6, 1, 0, 0); // two triangles
                    data->stats.draw_calls++;
                }
            }
        });
    }

    void Renderer3DLite::submit_debug_lines(const void *vertices, uint32_t vertex_count) {
        if (!s_res) return;
        const auto* bytes = static_cast<const uint8_t*>(vertices);
        s_res->debug_lines.insert(s_res->debug_lines.end(), bytes, bytes + (size_t)vertex_count * k_debug_vertex_size);
    }

    void Renderer3DLite::submit_icon(const Renderer3DInternal::IconDrawCommand &) {
    }

    // Lite draws are recorded by Renderer3DLite::record() from the lite.scene executor, after the whole
    // scene pass (including the editor overlay) has run; nothing to do at end_scene.
    void Renderer3DInternal::flush_lite_draws() {
    }
}