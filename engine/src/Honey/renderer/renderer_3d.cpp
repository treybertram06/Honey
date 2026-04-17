#include "hnpch.h"
#include "renderer_3d.h"
#include "render_command.h"
#include <glm/gtc/matrix_transform.hpp>

#include "pipeline.h"
#include "renderer.h"
#include "shader_cache.h"
#include "Honey/core/engine.h"
#include "Honey/core/settings.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_renderer_api.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

namespace Honey {

    namespace {
        struct PipelineVariantKey {
            void* render_pass = nullptr;
            uint8_t blend = 0;
            uint8_t cull_none = 0;

            bool operator==(const PipelineVariantKey& other) const {
                return render_pass == other.render_pass &&
                       blend == other.blend &&
                       cull_none == other.cull_none;
            }
        };

        struct PipelineVariantKeyHash {
            size_t operator()(const PipelineVariantKey& key) const {
                size_t h = std::hash<void*>{}(key.render_pass);
                h ^= (size_t)key.blend + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                h ^= (size_t)key.cull_none + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                return h;
            }
        };
    }

    struct MeshletDrawCommand {
        const Submesh* submesh   = nullptr;
        const Mesh*    mesh      = nullptr;
        Material*      material  = nullptr; // raw ptr — lifetime owned by Mesh/override containers
        glm::mat4      transform{1.0f};
        int            entity_id = -1;
    };

    struct Renderer3DData {
        static constexpr uint32_t max_textures  = 1024;  // keep in sync with VulkanRendererAPI::k_max_texture_slots

        GeometryPath                    geometry_path = GeometryPath::Meshlet;
        std::vector<MeshletDrawCommand> meshlet_draws;
        void* meshlet_set_layout = nullptr;
        void* meshlet_desc_pool  = nullptr;

        std::array<Ref<StorageBuffer>, VulkanContext::k_max_frames_in_flight> indirect_buffers{}; // VkDrawMeshTasksIndirectCommandEXT[]
        std::array<Ref<StorageBuffer>, VulkanContext::k_max_frames_in_flight> count_buffers{};    // uint32_t
        // draw_data buffers live per-mesh inside GlobalMeshletBuffers::draw_data_buffers

        // Persistent per-frame scratch — cleared each frame, never reallocated after warmup
        std::vector<GPUMaterial>                               frame_gpu_materials;
        std::unordered_map<Texture2D*, uint32_t>               frame_tex_slot_map;
        std::vector<const Mesh*>                               frame_mesh_order;
        std::unordered_map<const Mesh*, std::vector<uint32_t>> frame_draws_by_mesh;
        std::vector<VkDrawMeshTasksIndirectCommandEXT>         frame_indirect_cmds; // per-mesh scratch
        std::vector<GPUDrawData>                               frame_draw_data;      // per-mesh scratch
        VulkanContext*                                         vk_context_cache = nullptr;

        // Texture slots
        uint32_t                       max_texture_slots = 0;
        std::vector<Ref<Texture2D>>    texture_slots;
        uint32_t                       texture_slot_index = 1; // 0 is white tex
        Ref<Texture2D>                 white_texture;

        std::vector<VulkanRendererAPI::GlobalsState> vk_globals_stack;

        Ref<ShaderCache> shader_cache;

        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_forward_pipelines;
        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_meshlet_pipelines;

        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_gbuffer_pipelines;
        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_lighting_pipelines;

        // Cached scene data forwarded to the deferred lighting pass
        glm::mat4    scene_view_proj{1.0f};
        glm::vec3    scene_camera_pos{};
        LightsUBO    scene_lights{};
        Ref<Framebuffer> current_gbuffer_fb;

        Ref<Material> default_material;

        struct BatchKeyHash {
            size_t operator()(const Renderer3D::BatchKey& k) const {
                size_t h1 = std::hash<const void*>{}(k.va);
                size_t h2 = std::hash<const void*>{}(k.mat);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
            }
        };

        std::unordered_map<Renderer3D::BatchKey, Renderer3D::BatchValue, BatchKeyHash> batches;

        Ref<VertexBuffer> instance_vb;
        uint32_t instance_vb_capacity = 0; // in instances

        Renderer3D::Statistics stats;
        std::unordered_set<const void*> unique_meshes_this_frame;
    };

    static Renderer3DData* s_data;

    void Renderer3D::init() {
        HN_PROFILE_FUNCTION();

        if (!s_data)
            s_data = new Renderer3DData;

        auto& rs = Settings::get().renderer;

        s_data->geometry_path = rs.geometry_path;

        s_data->shader_cache = Renderer::get_shader_cache();

        s_data->default_material = Material::create();
        s_data->batches.clear();
        s_data->instance_vb.reset();
        s_data->instance_vb_capacity = 0;

        // Texture slot setup (slot 0 = white)
        s_data->max_texture_slots = VulkanRendererAPI::k_max_texture_slots;
        s_data->texture_slots.clear();
        s_data->texture_slots.resize(s_data->max_texture_slots);

        // If you already have a "white texture" helper elsewhere, swap this line to use it.
        // For now: create a tiny white texture.
        s_data->white_texture = Texture2D::create(1, 1);
        {
            const uint32_t white = 0xFFFFFFFFu;
            s_data->white_texture->set_data((void*)&white, sizeof(uint32_t));
        }
        s_data->texture_slots[0] = s_data->white_texture;

        // Default material uses white unless otherwise set.
        s_data->default_material->set_base_color_texture(nullptr);
        s_data->default_material->set_base_color_factor(glm::vec4(1.0f));

    }

    void Renderer3D::shutdown() {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() == RendererAPI::API::vulkan)
            VulkanRendererAPI::destroy_meshlet_resources();

        delete s_data;
    }

    void Renderer3D::begin_scene(const PerspectiveCamera& camera) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(false, "Renderer3D::begin_scene: Not implemented yet!");
    }

    void Renderer3D::begin_scene(const EditorCamera& camera) {
        HN_PROFILE_FUNCTION();
        reset_stats();

        CameraUBO camera_ubo{};
        camera_ubo.position = camera.get_position();
        camera_ubo.view_proj = camera.get_view_projection_matrix();

        s_data->scene_view_proj  = camera_ubo.view_proj;
        s_data->scene_camera_pos = camera_ubo.position;

        auto state = VulkanRendererAPI::get_globals_state();
        state.source = VulkanRendererAPI::GlobalsState::Source::Renderer3D;
        s_data->vk_globals_stack.push_back(state);
        VulkanRendererAPI::submit_camera(camera_ubo);

        // Reset frame texture table (keep white bound at slot 0)
        s_data->texture_slot_index = 1;
        if (!s_data->texture_slots.empty())
            s_data->texture_slots[0] = s_data->white_texture;

        s_data->batches.clear();
        s_data->unique_meshes_this_frame.clear();
    }

    void Renderer3D::begin_scene(const Camera& camera, const glm::mat4& transform) {
        HN_PROFILE_FUNCTION();
        reset_stats();

        CameraUBO camera_ubo{};
        camera_ubo.position = camera.get_position();
        camera_ubo.view_proj = camera.get_view_projection_matrix();

        auto state = VulkanRendererAPI::get_globals_state();
        state.source = VulkanRendererAPI::GlobalsState::Source::Renderer3D;
        s_data->vk_globals_stack.push_back(state);
        VulkanRendererAPI::submit_camera(camera_ubo);

        // Reset frame texture table (keep white bound at slot 0)
        s_data->texture_slot_index = 1;
        if (!s_data->texture_slots.empty())
            s_data->texture_slots[0] = s_data->white_texture;

        s_data->batches.clear();
        s_data->unique_meshes_this_frame.clear();
    }

    void Renderer3D::begin_scene(const glm::mat4& view_proj, const glm::vec3& position) {
        HN_PROFILE_FUNCTION();
        reset_stats();

        CameraUBO camera_ubo{};
        camera_ubo.position = position;
        camera_ubo.view_proj = view_proj;

        s_data->scene_view_proj  = view_proj;
        s_data->scene_camera_pos = position;

        auto state = VulkanRendererAPI::get_globals_state();
        state.source = VulkanRendererAPI::GlobalsState::Source::Renderer3D;
        s_data->vk_globals_stack.push_back(state);
        VulkanRendererAPI::submit_camera(camera_ubo);

        // Reset frame texture table (keep white bound at slot 0)
        s_data->texture_slot_index = 1;
        if (!s_data->texture_slots.empty())
            s_data->texture_slots[0] = s_data->white_texture;

        s_data->batches.clear();
        s_data->unique_meshes_this_frame.clear();
    }

    // Per-instance data uploaded to the vertex buffer.
    // Layout must match the vertex shader inputs at locations 3-7.
    struct InstanceData {
        glm::mat4 model;       // locations 3-6 (4 x vec4)
        int32_t   entity_id;   // location 7 (int)
    };
    static_assert(sizeof(InstanceData) == 68, "InstanceData size mismatch");

    static void ensure_instance_buffer_capacity(uint32_t required_instances) {
        HN_CORE_ASSERT(s_data, "Renderer3D: s_data null");

        if (required_instances == 0)
            return;

        if (!s_data->instance_vb || s_data->instance_vb_capacity < required_instances) {
            // grow (simple doubling strategy)
            uint32_t new_cap = std::max(64u, s_data->instance_vb_capacity);
            while (new_cap < required_instances) new_cap *= 2;

            s_data->instance_vb_capacity = new_cap;

            const uint32_t bytes = s_data->instance_vb_capacity * sizeof(InstanceData);
            s_data->instance_vb = VertexBuffer::create(bytes);

            s_data->instance_vb->set_layout({
                { ShaderDataType::Float4, "a_iModel0",   false, true },
                { ShaderDataType::Float4, "a_iModel1",   false, true },
                { ShaderDataType::Float4, "a_iModel2",   false, true },
                { ShaderDataType::Float4, "a_iModel3",   false, true },
                { ShaderDataType::Int,    "a_iEntityID", false, true },
            });
        }
    }

    static Ref<Pipeline> get_or_create_forward_pipeline(void* rp_native, bool blend, bool cull_none) {
        HN_CORE_ASSERT(rp_native, "get_or_create_forward_pipeline: rp_native is null");
        PipelineVariantKey key{rp_native, (uint8_t)(blend ? 1 : 0), (uint8_t)(cull_none ? 1 : 0)};

        auto it = s_data->vk_forward_pipelines.find(key);
        if (it != s_data->vk_forward_pipelines.end())
            return it->second;

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_Forward.glsl");
        if (!spec.perColorAttachmentBlend.empty())
            spec.perColorAttachmentBlend[0].enabled = blend;
        if (cull_none)
            spec.cullMode = CullMode::None;

        auto pipeline = Pipeline::create(spec, rp_native);
        s_data->vk_forward_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    static Ref<Pipeline> get_or_create_meshlet_pipeline(void* rp_native, void* extra_layout, bool blend, bool cull_none) {
        HN_CORE_ASSERT(rp_native, "get_or_create_meshlet_pipeline: rp_native is null");
        PipelineVariantKey key{rp_native, (uint8_t)(blend ? 1 : 0), (uint8_t)(cull_none ? 1 : 0)};

        auto it = s_data->vk_meshlet_pipelines.find(key);
        if (it != s_data->vk_meshlet_pipelines.end())
            return it->second;

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_Meshlet.glsl");
        if (!spec.perColorAttachmentBlend.empty())
            spec.perColorAttachmentBlend[0].enabled = blend;
        if (cull_none)
            spec.cullMode = CullMode::None;

        auto pipeline = Pipeline::create(spec, rp_native, extra_layout);
        s_data->vk_meshlet_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    static Ref<Pipeline> get_or_create_gbuffer_pipeline(void* rp_native, bool blend, bool cull_none) {
        PipelineVariantKey key{rp_native, 0, (uint8_t)(cull_none ? 1 : 0)};

        auto it = s_data->vk_gbuffer_pipelines.find(key);
        if (it != s_data->vk_gbuffer_pipelines.end())
            return it->second;

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_DeferredGeometry.glsl");
        // G-Buffer has 3 color attachments (gAlbedo, gNormal, gPBRParams), all opaque.
        // from_shader hardcodes 2 blend states for offscreen passes (editor FB hack), TODO: fix this 
        // so we override to exactly 3 here.
        spec.perColorAttachmentBlend.clear();
        spec.perColorAttachmentBlend.resize(3, AttachmentBlendState{});
        if (cull_none)
            spec.cullMode = CullMode::None;

        auto pipeline = Pipeline::create(spec, rp_native);
        s_data->vk_gbuffer_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    static glm::vec4 slot_scale_offset(const Material::TextureSlot& slot) {
        return glm::vec4(slot.transform.scale.x, slot.transform.scale.y, slot.transform.offset.x, slot.transform.offset.y);
    }

    static int32_t register_material_texture(Texture2D* tex,
                                             std::unordered_map<Texture2D*, uint32_t>& tex_slot_map,
                                             uint32_t& tex_slot_count) {
        if (!tex)
            return -1;

        auto [tex_it, inserted] = tex_slot_map.try_emplace(tex, 0u);
        if (inserted) {
            if (tex_slot_count >= s_data->max_texture_slots) {
                tex_it->second = 0u;
                return 0;
            }
            tex_it->second = tex_slot_count++;
        }
        return (int32_t)tex_it->second;
    }

    static GPUMaterial build_gpu_material(const Material* mat,
                                          std::unordered_map<Texture2D*, uint32_t>& tex_slot_map,
                                          uint32_t& tex_slot_count) {
        GPUMaterial gpu{};
        if (!mat) {
            gpu.base_color_tex_id = register_material_texture(s_data->white_texture.get(), tex_slot_map, tex_slot_count);
            return gpu;
        }

        const auto& pbr = mat->pbr();
        gpu.base_color = pbr.base_color_factor;
        gpu.emissive_factor = glm::vec4(
            pbr.emissive_factor * pbr.extensions.emissive_strength.strength, 1.0f);
        gpu.metallic = pbr.metallic_factor;
        gpu.roughness = pbr.roughness_factor;
        gpu.normal_scale = pbr.normal_scale;
        gpu.occlusion_strength = pbr.occlusion_strength;
        gpu.alpha_cutoff = pbr.alpha_cutoff;
        gpu.alpha_mode = (int32_t)pbr.alpha_mode;
        gpu.double_sided = pbr.double_sided ? 1 : 0;
        gpu.unlit = pbr.extensions.unlit.enabled ? 1 : 0;

        Texture2D* base_tex = pbr.base_color_texture.texture ? pbr.base_color_texture.texture.get() : s_data->white_texture.get();
        gpu.base_color_tex_id = register_material_texture(base_tex, tex_slot_map, tex_slot_count);
        gpu.metallic_roughness_tex_id = register_material_texture(
            pbr.metallic_roughness_texture.texture.get(), tex_slot_map, tex_slot_count);
        gpu.normal_tex_id = register_material_texture(
            pbr.normal_texture.texture.get(), tex_slot_map, tex_slot_count);
        gpu.occlusion_tex_id = register_material_texture(
            pbr.occlusion_texture.texture.get(), tex_slot_map, tex_slot_count);
        gpu.emissive_tex_id = register_material_texture(
            pbr.emissive_texture.texture.get(), tex_slot_map, tex_slot_count);

        gpu.base_color_uv_set = pbr.base_color_texture.tex_coord;
        gpu.metallic_roughness_uv_set = pbr.metallic_roughness_texture.tex_coord;
        gpu.normal_uv_set = pbr.normal_texture.tex_coord;
        gpu.occlusion_uv_set = pbr.occlusion_texture.tex_coord;
        gpu.emissive_uv_set = pbr.emissive_texture.tex_coord;

        gpu.base_color_uv_scale_offset = slot_scale_offset(pbr.base_color_texture);
        gpu.metallic_roughness_uv_scale_offset = slot_scale_offset(pbr.metallic_roughness_texture);
        gpu.normal_uv_scale_offset = slot_scale_offset(pbr.normal_texture);
        gpu.occlusion_uv_scale_offset = slot_scale_offset(pbr.occlusion_texture);
        gpu.emissive_uv_scale_offset = slot_scale_offset(pbr.emissive_texture);

        gpu.base_color_uv_rotation = pbr.base_color_texture.transform.rotation;
        gpu.metallic_roughness_uv_rotation = pbr.metallic_roughness_texture.transform.rotation;
        gpu.normal_uv_rotation = pbr.normal_texture.transform.rotation;
        gpu.occlusion_uv_rotation = pbr.occlusion_texture.transform.rotation;
        gpu.emissive_uv_rotation = pbr.emissive_texture.transform.rotation;
        return gpu;
    }

    using PipelineFactory = std::function<Ref<Pipeline>(void* rp, bool blend, bool cull_none)>;
    static void flush_batches_vulkan(const PipelineFactory& get_pipeline) {
        HN_PROFILE_FUNCTION();

        if (!s_data->vk_context_cache) {
            auto* base = Application::get().get_window().get_context();
            s_data->vk_context_cache = dynamic_cast<Honey::VulkanContext*>(base);
            HN_CORE_ASSERT(s_data->vk_context_cache, "Renderer3D Vulkan path expected VulkanContext");
        }
        auto* vkCtx = s_data->vk_context_cache;

        CameraUBO saved_camera = VulkanRendererAPI::get_globals_state().cameraUBO;

        void* rpNative = nullptr;
        if (auto target = Renderer::get_render_target()) {
            auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
            HN_CORE_ASSERT(vk_fb, "Renderer3D: current render target is not a VulkanFramebuffer");
            rpNative = vk_fb->get_render_pass();          // editor / offscreen
        } else {
            rpNative = vkCtx->get_render_pass();          // main swapchain
        }
        HN_CORE_ASSERT(rpNative, "Renderer3D: rpNative is null");

        // --- Pack all instance transforms into one contiguous array ---
        uint32_t total_instances = 0;
        for (auto& [key, batch] : s_data->batches) {
            total_instances += (uint32_t)batch.transforms.size();
        }

        if (total_instances == 0)
            return;

        std::vector<InstanceData> packed;
        packed.reserve(total_instances);

        // Keep start index per batch, in the SAME iteration order used for packing
        std::vector<std::pair<const Renderer3D::BatchKey*, uint32_t>> starts;
        starts.reserve(s_data->batches.size());

        struct OrderedBatch {
            const Renderer3D::BatchKey* key;
            const Renderer3D::BatchValue* batch;
            uint32_t start_index;
        };
        std::vector<OrderedBatch> ordered_batches;
        ordered_batches.reserve(s_data->batches.size());

        for (auto& [key, batch] : s_data->batches) {
            if (batch.transforms.empty())
                continue;

            const uint32_t start_index = (uint32_t)packed.size();
            starts.emplace_back(&key, start_index);
            for (size_t i = 0; i < batch.transforms.size(); ++i) {
                InstanceData inst{};
                inst.model     = batch.transforms[i];
                inst.entity_id = (i < batch.entity_ids.size()) ? batch.entity_ids[i] : -1;
                packed.push_back(inst);
            }

            ordered_batches.emplace_back(OrderedBatch{&key, &batch, start_index});
        }

        HN_CORE_ASSERT(packed.size() == total_instances,
                       "Renderer3D: packed instance count mismatch (packed={}, expected={})",
                       packed.size(), total_instances);

        // Upload ONCE for the whole frame
        ensure_instance_buffer_capacity((uint32_t)packed.size());
        s_data->instance_vb->set_data(packed.data(), (uint32_t)(packed.size() * sizeof(InstanceData)));

        struct MaterialPC { int32_t material_index; int32_t _pad[3]; };
        static_assert(sizeof(MaterialPC) <= 128, "MaterialPC too large");

        // Build global texture table for all batches (bindless — up to k_max_texture_slots)
        std::unordered_map<Texture2D*, uint32_t> tex_slot_map;
        tex_slot_map[s_data->white_texture.get()] = 0;
        uint32_t tex_slot_count = 1;

        std::vector<GPUMaterial> all_materials;
        all_materials.reserve(ordered_batches.size());

        for (uint32_t i = 0; i < (uint32_t)ordered_batches.size(); i++) {
            auto* mat = ordered_batches[i].batch->material.get();
            all_materials.push_back(build_gpu_material(mat, tex_slot_map, tex_slot_count));
        }

        // Build flat texture array and submit globals once for the whole frame
        std::array<void*, VulkanRendererAPI::k_max_texture_slots> tex_array{};
        tex_array[0] = s_data->white_texture.get();
        for (auto& [tex_ptr, slot] : tex_slot_map)
            tex_array[slot] = tex_ptr;

        // Bind at least one pipeline before globals upload so a valid pipeline
        // layout is available for descriptor binds.
        if (!ordered_batches.empty()) {
            const auto* first_mat = ordered_batches[0].batch->material.get();
            const bool blend = first_mat && first_mat->get_alpha_mode() == Material::AlphaMode::Blend;
            const bool cull_none = first_mat && first_mat->get_double_sided();
            Ref<Pipeline> first_pipe = get_pipeline(rpNative, blend, cull_none);
            RenderCommand::bind_pipeline(first_pipe);
            s_data->stats.pipeline_binds++;
        }

        VulkanRendererAPI::submit_camera(saved_camera);
        VulkanRendererAPI::submit_bound_textures(tex_array, tex_slot_count);
        VulkanRendererAPI::submit_materials(all_materials, 0);
        VulkanRendererAPI::flush_globals();

        // Emit one draw call per batch
        Ref<Pipeline> current_pipe;
        for (uint32_t i = 0; i < (uint32_t)ordered_batches.size(); i++) {
            const auto* mat = ordered_batches[i].batch->material.get();
            const bool blend = mat && mat->get_alpha_mode() == Material::AlphaMode::Blend;
            const bool cull_none = mat && mat->get_double_sided();
            Ref<Pipeline> pipe = get_pipeline(rpNative, blend, cull_none);

            if (pipe != current_pipe) {
                RenderCommand::bind_pipeline(pipe);
                s_data->stats.pipeline_binds++;
                current_pipe = pipe;
            }

            const int32_t material_index = (int32_t)i;
            const uint32_t byte_offset = ordered_batches[i].start_index * (uint32_t)sizeof(InstanceData);

            HN_CORE_ASSERT((byte_offset % 4u) == 0u, "Renderer3D: instance byte offset must be 4-byte aligned");

            MaterialPC pc{material_index};
            VulkanRendererAPI::submit_push_constants(&pc, sizeof(MaterialPC));

            VulkanRendererAPI::submit_instanced_draw(
                ordered_batches[i].batch->va,
                s_data->instance_vb,
                0,
                (uint32_t)ordered_batches[i].batch->transforms.size(),
                byte_offset
            );

            s_data->stats.draw_calls++;
        }
    }

    void flush_meshlet_draws() {
        HN_PROFILE_FUNCTION();

        if (s_data->meshlet_draws.empty())
            return;

        // Cache VulkanContext* — stays valid for the lifetime of the renderer
        if (!s_data->vk_context_cache) {
            auto* base = Application::get().get_window().get_context();
            s_data->vk_context_cache = dynamic_cast<Honey::VulkanContext*>(base);
            HN_CORE_ASSERT(s_data->vk_context_cache, "flush_meshlet_draws: expected VulkanContext");
        }
        auto* vkCtx = s_data->vk_context_cache;

        void* rpNative = nullptr;
        if (auto target = Renderer::get_render_target()) {
            auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
            HN_CORE_ASSERT(vk_fb, "flush_meshlet_draws: render target is not a VulkanFramebuffer");
            rpNative = vk_fb->get_render_pass();
        } else {
            rpNative = vkCtx->get_render_pass();
        }
        HN_CORE_ASSERT(rpNative, "flush_meshlet_draws: rpNative is null");

        void* extra_layout = VulkanRendererAPI::get_or_create_meshlet_set_layout();

        // --- Build global material + texture table ---
        CameraUBO saved_camera = VulkanRendererAPI::get_globals_state().cameraUBO;

        // Reuse persistent containers — no heap alloc after the first frame
        auto& tex_slot_map  = s_data->frame_tex_slot_map;
        auto& gpu_materials = s_data->frame_gpu_materials;
        tex_slot_map.clear();
        gpu_materials.clear();
        tex_slot_map[s_data->white_texture.get()] = 0;
        uint32_t tex_slot_count = 1;

        gpu_materials.reserve(s_data->meshlet_draws.size());

        for (const auto& cmd : s_data->meshlet_draws) {
            auto* mat = cmd.material;
            gpu_materials.push_back(build_gpu_material(mat, tex_slot_map, tex_slot_count));
        }

        std::array<void*, VulkanRendererAPI::k_max_texture_slots> tex_array{};
        tex_array[0] = s_data->white_texture.get();
        for (auto& [tex_ptr, slot] : tex_slot_map)
            tex_array[slot] = tex_ptr;

        // Bind one compatible meshlet pipeline before globals upload so
        // descriptor writes have a valid pipeline layout bound.
        if (!s_data->meshlet_draws.empty()) {
            const auto* first_mat = s_data->meshlet_draws[0].material;
            const bool blend = first_mat && first_mat->get_alpha_mode() == Material::AlphaMode::Blend;
            const bool cull_none = first_mat && first_mat->get_double_sided();
            Ref<Pipeline> first_pipe = get_or_create_meshlet_pipeline(rpNative, extra_layout, blend, cull_none);
            RenderCommand::bind_pipeline(first_pipe);
            s_data->stats.pipeline_binds++;
        }

        VulkanRendererAPI::submit_camera(saved_camera);
        VulkanRendererAPI::submit_bound_textures(tex_array, tex_slot_count);
        VulkanRendererAPI::submit_materials(gpu_materials, 0);
        VulkanRendererAPI::flush_globals();

        // --- Group draws by Mesh ---
        auto& mesh_order    = s_data->frame_mesh_order;
        auto& draws_by_mesh = s_data->frame_draws_by_mesh;
        mesh_order.clear();
        draws_by_mesh.clear();

        for (uint32_t i = 0; i < (uint32_t)s_data->meshlet_draws.size(); ++i) {
            const Mesh* m = s_data->meshlet_draws[i].mesh;
            auto [dm_it, dm_inserted] = draws_by_mesh.try_emplace(m);
            if (dm_inserted)
                mesh_order.push_back(m);
            dm_it->second.push_back(i);
        }

        // Size shared indirect + count buffers
        const uint32_t total_draws    = (uint32_t)s_data->meshlet_draws.size();
        const uint32_t indirect_bytes = total_draws * 12u; // sizeof(VkDrawMeshTasksIndirectCommandEXT)
        const uint32_t count_bytes    = std::max(total_draws, 1u) * (uint32_t)sizeof(uint32_t);

        const uint32_t frame_slot = vkCtx->get_current_frame() % VulkanContext::k_max_frames_in_flight;
        auto& indirect_buffer = s_data->indirect_buffers[frame_slot];
        auto& count_buffer = s_data->count_buffers[frame_slot];

        if (!indirect_buffer || indirect_buffer->get_size() < indirect_bytes)
            indirect_buffer = StorageBuffer::create(indirect_bytes,
                StorageBufferUsage::Dynamic | StorageBufferUsage::Indirect);

        if (!count_buffer || count_buffer->get_size() < count_bytes)
            count_buffer = StorageBuffer::create(std::max(count_bytes, 4u),
                StorageBufferUsage::Dynamic | StorageBufferUsage::Indirect);

        auto indirect_vk = reinterpret_cast<VkBuffer>(indirect_buffer->get_native_buffer());
        auto count_vk    = reinterpret_cast<VkBuffer>(count_buffer->get_native_buffer());

        // Per-mesh scratch vectors — reused each iteration, capacity is retained between frames
        auto& indirect_cmds = s_data->frame_indirect_cmds;
        auto& draw_data     = s_data->frame_draw_data;

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

            // IMPORTANT: size and bind draw_data buffer once per mesh before issuing
            // any variant dispatches. Reallocating mid-loop can invalidate buffers
            // still referenced by commands already recorded this frame.
            VulkanRendererAPI::update_mesh_draw_data_binding(bufs, mesh_total_draws);
            VulkanRendererAPI::ensure_mesh_descriptor_set(bufs);

            // Split this mesh's draws by material state so culling/blending pipeline
            // variants are applied per draw subset, not per whole mesh.
            std::unordered_map<uint8_t, std::vector<uint32_t>> draws_by_variant;
            std::vector<uint8_t> variant_order;
            draws_by_variant.reserve(4);
            variant_order.reserve(4);

            for (uint32_t draw_idx : indices) {
                const auto* draw_mat = s_data->meshlet_draws[draw_idx].material;
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

                Ref<Pipeline> pipe = get_or_create_meshlet_pipeline(rpNative, extra_layout, blend, cull_none);
                if (pipe != current_pipe) {
                    RenderCommand::bind_pipeline(pipe);
                    s_data->stats.pipeline_binds++;
                    current_pipe = pipe;
                }

                indirect_cmds.clear();
                draw_data.clear();
                indirect_cmds.reserve(mesh_draw_count);
                draw_data.reserve(mesh_draw_count);

                const uint32_t mesh_draw_base = mesh_local_draw_offset;
                for (uint32_t local_i = 0; local_i < mesh_draw_count; ++local_i) {
                    const uint32_t draw_idx = variant_draws[local_i];
                    const auto& cmd = s_data->meshlet_draws[draw_idx];
                    const auto& geo = *cmd.submesh->meshlets;
                    indirect_cmds.push_back({ geo.meshlet_count, 1, 1 });
                    draw_data.push_back({
                        cmd.transform,
                        geo.meshlets_offset,
                        geo.meshlet_count,
                        (int32_t)draw_idx, // index into gpu_materials (original submission order)
                        cmd.entity_id
                    });
                }

                const uint32_t indirect_byte_off = global_draw_offset * 12u;
                const uint32_t count_byte_off = dispatch_counter * (uint32_t)sizeof(uint32_t);

                indirect_buffer->set_data(indirect_cmds.data(),
                    mesh_draw_count * 12u, indirect_byte_off);
                count_buffer->set_data(&mesh_draw_count, sizeof(uint32_t), count_byte_off);

                // gl_DrawID resets to 0 for this dispatch, so per-dispatch draw_data starts at 0.
                Ref<StorageBuffer> draw_data_buffer = VulkanRendererAPI::get_mesh_draw_data_buffer(bufs);
                HN_CORE_ASSERT(draw_data_buffer, "flush_meshlet_draws: draw_data_buffer not available for frame slot");
                draw_data_buffer->set_data(draw_data.data(),
                    mesh_draw_count * (uint32_t)sizeof(GPUDrawData),
                    mesh_draw_base * (uint32_t)sizeof(GPUDrawData));

                struct MeshletPC { int32_t draw_data_base; int32_t _pad[3]; };
                const MeshletPC pc{(int32_t)mesh_draw_base, {0, 0, 0}};
                VulkanRendererAPI::submit_push_constants(
                    &pc, sizeof(MeshletPC), 0,
                    VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT | VK_SHADER_STAGE_FRAGMENT_BIT);

                VulkanRendererAPI::submit_set1_descriptor_set(
                    VulkanRendererAPI::get_mesh_descriptor_set(bufs),
                    pipe->get_native_pipeline_layout());

                VulkanRendererAPI::submit_mesh_tasks_indirect_count(
                    indirect_vk, indirect_byte_off,
                    count_vk, count_byte_off,
                    mesh_draw_count, 12u);

                s_data->stats.draw_calls++;
                mesh_local_draw_offset += mesh_draw_count;
                global_draw_offset += mesh_draw_count;
                dispatch_counter++;
            }
        }

        s_data->meshlet_draws.clear();
    }

    void Renderer3D::end_scene() {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            HN_CORE_ASSERT(false, "Renderer3D::end_scene: only Vulkan path implemented");
        }

        auto renderer_type = Settings::get().renderer.renderer_type;

        switch (renderer_type) {
        case RendererSettings::RendererType::forward:
            flush_batches_vulkan(get_or_create_forward_pipeline);
            flush_meshlet_draws();
            break;

        case RendererSettings::RendererType::deferred:
            flush_batches_vulkan(get_or_create_gbuffer_pipeline);
            flush_meshlet_draws();
            break;

        default:
            HN_CORE_ASSERT(false, "Renderer3D::end_scene: unknown renderer type");
            break;
        }

        HN_CORE_ASSERT(!s_data->vk_globals_stack.empty(),
                           "Renderer3D Vulkan globals stack underflow (end_scene without matching begin_scene)");
        VulkanRendererAPI::set_globals_state(s_data->vk_globals_stack.back());
        s_data->vk_globals_stack.pop_back();

    }

    void Renderer3D::submit_lights(const LightsUBO& lights) {
        HN_PROFILE_FUNCTION();
        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            HN_CORE_WARN("Renderer3D::submit_lights: only Vulkan path implemented");
            return;
        }
        s_data->scene_lights = lights;
        VulkanRendererAPI::submit_lights(lights);
    }

    static Ref<Pipeline> get_or_create_lighting_pipeline(void* rp_native) {
        PipelineVariantKey key{rp_native, 0, 0};
        auto it = s_data->vk_lighting_pipelines.find(key);
        if (it != s_data->vk_lighting_pipelines.end())
            return it->second;

        auto* base = Application::get().get_window().get_context();
        auto* vk = dynamic_cast<VulkanContext*>(base);
        HN_CORE_ASSERT(vk, "get_or_create_lighting_pipeline: VulkanContext is null");

        void* gbuffer_layout = vk->get_gbuffer_set_layout();

        auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_DeferredLighting.glsl");
        spec.depthStencil.depthTest  = false;
        spec.depthStencil.depthWrite = false;
        // editorViewport has 2 color attachments (color + entity ID); resize to match.
        // The lighting shader writes color at location=0 and -1 at location=1 to preserve entity ID buffer.
        spec.perColorAttachmentBlend.clear();
        spec.perColorAttachmentBlend.resize(2, AttachmentBlendState{});

        auto pipeline = Pipeline::create(spec, rp_native, gbuffer_layout);
        s_data->vk_lighting_pipelines.emplace(key, pipeline);
        return pipeline;
    }

    void Renderer3D::begin_deferred_lighting_scene(Ref<Framebuffer> gbuffer_fb) {
        HN_CORE_ASSERT(s_data, "Renderer3D not initialized");
        s_data->current_gbuffer_fb = gbuffer_fb;
    }

    void Renderer3D::flush_deferred_lighting() {
        HN_CORE_ASSERT(s_data, "Renderer3D not initialized");
        HN_CORE_ASSERT(s_data->current_gbuffer_fb, "flush_deferred_lighting: no gbuffer_fb set — call begin_deferred_lighting_scene first");

        if (Renderer::get_api() != RendererAPI::API::vulkan)
            return;

        if (!s_data->vk_context_cache) {
            auto* base = Application::get().get_window().get_context();
            s_data->vk_context_cache = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(s_data->vk_context_cache, "flush_deferred_lighting: VulkanContext null");
        }
        auto* vkCtx = s_data->vk_context_cache;

        void* rpNative = nullptr;
        if (auto target = Renderer::get_render_target()) {
            auto* vk_fb = dynamic_cast<VulkanFramebuffer*>(target.get());
            HN_CORE_ASSERT(vk_fb, "flush_deferred_lighting: render target is not VulkanFramebuffer");
            rpNative = vk_fb->get_render_pass();
        } else {
            rpNative = vkCtx->get_render_pass();
        }
        HN_CORE_ASSERT(rpNative, "flush_deferred_lighting: rpNative is null");

        Ref<Pipeline> pipe = get_or_create_lighting_pipeline(rpNative);
        RenderCommand::bind_pipeline(pipe);

        // Emit BindGlobals to bind set=0 with the lighting pipeline's layout.
        // Use the scene camera/lights cached from begin_scene/submit_lights.
        // Submit one white texture so sampler+texture descriptors are populated.
        CameraUBO cam_ubo{};
        cam_ubo.view_proj = s_data->scene_view_proj;
        cam_ubo.position  = s_data->scene_camera_pos;
        VulkanRendererAPI::submit_camera(cam_ubo);
        VulkanRendererAPI::submit_lights(s_data->scene_lights);

        std::array<void*, VulkanRendererAPI::k_max_texture_slots> tex_array{};
        tex_array[0] = s_data->white_texture.get();
        VulkanRendererAPI::submit_bound_textures(tex_array, 1);
        VulkanRendererAPI::flush_globals();

        // Bind set=1 (G-buffer textures) and draw a fullscreen triangle
        auto* gbuffer_vk = dynamic_cast<VulkanFramebuffer*>(s_data->current_gbuffer_fb.get());
        HN_CORE_ASSERT(gbuffer_vk, "flush_deferred_lighting: current_gbuffer_fb is not a VulkanFramebuffer");

        uint32_t frame = vkCtx->get_current_frame();
        vkCtx->update_gbuffer_descriptors(frame, gbuffer_vk);
        VkDescriptorSet gbuf_ds = vkCtx->get_gbuffer_descriptor_set(frame);

        VkPipelineLayout pipe_layout = static_cast<VkPipelineLayout>(pipe->get_native_pipeline_layout());

        vkCtx->queue_custom_vulkan_cmd(
            [gbuf_ds, pipe_layout](VkCommandBuffer cmd, uint32_t, uint32_t) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipe_layout, 1, 1, &gbuf_ds, 0, nullptr);
                vkCmdDraw(cmd, 3, 1, 0, 0);  // fullscreen triangle (no vertex buffer)
            }
        );
    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array, const glm::mat4& transform, int entity_id) {
        draw_mesh(vertex_array, s_data->default_material, transform, entity_id);
    }

    void Renderer3D::draw_mesh(const Ref<VertexArray>& vertex_array, const Ref<Material>& material, const glm::mat4& transform, int entity_id) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(vertex_array, "Renderer3D::draw_mesh: vertex_array is null");
        HN_CORE_ASSERT(material, "Renderer3D::draw_mesh: material is null");

        s_data->stats.mesh_submissions++;

        // unique mesh stats
        s_data->unique_meshes_this_frame.insert(vertex_array.get());
        s_data->stats.unique_meshes = (uint32_t)s_data->unique_meshes_this_frame.size();

        BatchKey key{};
        key.va = vertex_array.get();
        key.mat = material.get();

        auto it = s_data->batches.find(key);
        if (it == s_data->batches.end()) {
            BatchValue v{};
            v.va = vertex_array;
            v.material = material;
            v.transforms.reserve(128);
            v.entity_ids.reserve(128);
            it = s_data->batches.emplace(key, std::move(v)).first;
        }

        it->second.transforms.push_back(transform);
        it->second.entity_ids.push_back(entity_id);
    }

    void Renderer3D::submit_submesh(const Submesh& submesh, const Ref<Material>& material,
        const glm::mat4& transform, int entity_id, const Mesh* mesh) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(material, "Renderer3D::submit_submesh: material is null");

        const bool can_use_meshlets =
            s_data->geometry_path == GeometryPath::Meshlet && submesh.meshlets.has_value();

        if (can_use_meshlets) {
            submit_meshlet_submesh(submesh, material, transform, entity_id, mesh);
            return;
        }

        HN_CORE_ASSERT(submesh.vao, "Renderer3D::submit_submesh: submesh.vao is null");
        draw_mesh(submesh.vao, material, transform, entity_id);
    }

    void Renderer3D::submit_meshlet_submesh(const Submesh& submesh, const Ref<Material>& material,
        const glm::mat4& transform, int entity_id, const Mesh* mesh) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(material, "Renderer3D::submit_meshlet_submesh: material is null");
        HN_CORE_ASSERT(submesh.meshlets.has_value(), "Renderer3D::submit_meshlet_submesh: submesh.meshlets is null");

        s_data->meshlet_draws.push_back(
            MeshletDrawCommand{
                .submesh   = &submesh,
                .mesh      = mesh,
                .material  = material.get(),
                .transform = transform,
                .entity_id = entity_id
            }
        );
    }

    void Renderer3D::prewarm_pipelines(void* native_render_pass) {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() != RendererAPI::API::vulkan)
            return;

        HN_CORE_ASSERT(native_render_pass, "Renderer3D::prewarm_pipelines: native_render_pass is null");

        // Force creation via the SAME code path used during rendering.
        //(void)get_or_create_vk_forward3d_pipeline(native_render_pass);
    }

    void Renderer3D::set_geometry_render_path(GeometryPath path) {
        s_data->geometry_path = path;
    }

    Renderer3D::Statistics Renderer3D::get_stats() {
        return s_data->stats;
    }

    void Renderer3D::reset_stats() {
        memset(&s_data->stats, 0, sizeof(Statistics));
        if (s_data)
            s_data->unique_meshes_this_frame.clear();
    }
}
