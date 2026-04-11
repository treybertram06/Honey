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

    struct MeshletDrawCommand {
        const Submesh* submesh = nullptr;
        const Mesh*    mesh    = nullptr;
        Ref<Material>  material;
        glm::mat4      transform{1.0f};
        int            entity_id = -1;
    };

    struct Renderer3DData {
        static constexpr uint32_t max_textures  = 1024;  // keep in sync with VulkanRendererAPI::k_max_texture_slots

        GeometryPath                    geometry_path = GeometryPath::Meshlet;
        std::vector<MeshletDrawCommand> meshlet_draws;
        void* meshlet_set_layout = nullptr;
        void* meshlet_desc_pool  = nullptr;

        Ref<StorageBuffer> indirect_buffer;     // VkDrawMeshTasksIndirectCommandEXT[]
        Ref<StorageBuffer> count_buffer;        // uint32_t
        // draw_data_buffer lives per-mesh inside GlobalMeshletBuffers::draw_data_buffer

        // Texture slots
        uint32_t                       max_texture_slots = 0;
        std::vector<Ref<Texture2D>>    texture_slots;
        uint32_t                       texture_slot_index = 1; // 0 is white tex
        Ref<Texture2D>                 white_texture;

        std::vector<VulkanRendererAPI::GlobalsState> vk_globals_stack;

        Ref<ShaderCache> shader_cache;

        std::unordered_map<void*, Ref<Pipeline>> vk_forward_pipelines;
        std::unordered_map<void*, Ref<Pipeline>> vk_meshlet_pipelines;

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

    static void flush_batches_vulkan() {
        HN_PROFILE_FUNCTION();

        auto* base = Application::get().get_window().get_context();
        auto* vkCtx = dynamic_cast<Honey::VulkanContext*>(base);
        HN_CORE_ASSERT(vkCtx, "Renderer3D Vulkan path expected VulkanContext");

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

        Ref<Pipeline> pipe;
        auto it = s_data->vk_forward_pipelines.find(rpNative);
        if (it == s_data->vk_forward_pipelines.end()) {
            auto pipeline = Pipeline::create(asset_root / "shaders" / "Renderer3D_Forward.glsl", rpNative);
            it = s_data->vk_forward_pipelines.emplace(rpNative, pipeline).first;
        }
        pipe = it->second;

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

        // One pipeline bind for all batches (same forward shader for now)
        RenderCommand::bind_pipeline(pipe);
        s_data->stats.pipeline_binds++;


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
            Texture2D* tex = (mat && mat->get_base_color_texture())
                             ? mat->get_base_color_texture().get()
                             : s_data->white_texture.get();

            if (!tex_slot_map.count(tex)) {
                if (tex_slot_count < s_data->max_texture_slots)
                    tex_slot_map[tex] = tex_slot_count++;
                else
                    tex_slot_map[tex] = 0; // hard cap (>1024 unique textures) — fall back to white
            }

            GPUMaterial gpu_mat{};
            gpu_mat.base_color        = mat ? mat->get_base_color_factor()  : glm::vec4(1.0f);
            gpu_mat.base_color_tex_id = (int32_t)tex_slot_map.at(tex);
            gpu_mat.metallic          = mat ? mat->get_metallic_factor()    : 0.0f;
            gpu_mat.roughness         = mat ? mat->get_roughness_factor()   : 0.5f;
            all_materials.push_back(gpu_mat);
        }

        // Build flat texture array and submit globals once for the whole frame
        std::array<void*, VulkanRendererAPI::k_max_texture_slots> tex_array{};
        tex_array[0] = s_data->white_texture.get();
        for (auto& [tex_ptr, slot] : tex_slot_map)
            tex_array[slot] = tex_ptr;

        VulkanRendererAPI::submit_camera(saved_camera);
        VulkanRendererAPI::submit_bound_textures(tex_array, tex_slot_count);
        VulkanRendererAPI::submit_materials(all_materials, 0);
        VulkanRendererAPI::flush_globals();

        // Emit one draw call per batch
        for (uint32_t i = 0; i < (uint32_t)ordered_batches.size(); i++) {
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

        auto* base  = Application::get().get_window().get_context();
        auto* vkCtx = dynamic_cast<Honey::VulkanContext*>(base);
        HN_CORE_ASSERT(vkCtx, "flush_meshlet_draws: expected VulkanContext");

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

        Ref<Pipeline> pipe;
        auto it = s_data->vk_meshlet_pipelines.find(rpNative);
        if (it == s_data->vk_meshlet_pipelines.end()) {
            auto pipeline = Pipeline::create(asset_root / "shaders" / "Renderer3D_Meshlet.glsl", rpNative, extra_layout);
            it = s_data->vk_meshlet_pipelines.emplace(rpNative, pipeline).first;
        }
        pipe = it->second;

        RenderCommand::bind_pipeline(pipe);

        // --- Build global material + texture table (shared across all draws) ---
        CameraUBO saved_camera = VulkanRendererAPI::get_globals_state().cameraUBO;

        std::unordered_map<Texture2D*, uint32_t> tex_slot_map;
        tex_slot_map[s_data->white_texture.get()] = 0;
        uint32_t tex_slot_count = 1;

        std::vector<GPUMaterial> gpu_materials;
        gpu_materials.reserve(s_data->meshlet_draws.size());

        for (const auto& cmd : s_data->meshlet_draws) {
            auto* mat = cmd.material.get();
            Texture2D* tex = (mat && mat->get_base_color_texture())
                ? mat->get_base_color_texture().get()
                : s_data->white_texture.get();

            if (!tex_slot_map.count(tex)) {
                if (tex_slot_count < s_data->max_texture_slots)
                    tex_slot_map[tex] = tex_slot_count++;
                else
                    tex_slot_map[tex] = 0; // hard cap (>1024 unique textures) — fall back to white
            }

            GPUMaterial gpu_mat{};
            gpu_mat.base_color        = mat ? mat->get_base_color_factor() : glm::vec4(1.0f);
            gpu_mat.base_color_tex_id = (int32_t)tex_slot_map.at(tex);
            gpu_mat.metallic          = mat ? mat->get_metallic_factor()   : 0.0f;
            gpu_mat.roughness         = mat ? mat->get_roughness_factor()  : 0.5f;
            gpu_materials.push_back(gpu_mat);
        }

        std::array<void*, VulkanRendererAPI::k_max_texture_slots> tex_array{};
        tex_array[0] = s_data->white_texture.get();
        for (auto& [tex_ptr, slot] : tex_slot_map)
            tex_array[slot] = tex_ptr;

        VulkanRendererAPI::submit_camera(saved_camera);
        VulkanRendererAPI::submit_bound_textures(tex_array, tex_slot_count);
        VulkanRendererAPI::submit_materials(gpu_materials, 0);
        VulkanRendererAPI::flush_globals();

        // --- Group draws by Mesh, then dispatch one indirect call per Mesh ---

        // Collect unique meshes in submission order
        std::vector<const Mesh*> mesh_order;
        std::unordered_map<const Mesh*, std::vector<uint32_t>> draws_by_mesh;
        for (uint32_t i = 0; i < (uint32_t)s_data->meshlet_draws.size(); ++i) {
            const Mesh* m = s_data->meshlet_draws[i].mesh;
            if (!draws_by_mesh.count(m))
                mesh_order.push_back(m);
            draws_by_mesh[m].push_back(i);
        }

        // Size shared indirect + count buffers (draw_data is now per-mesh)
        const uint32_t total_draws    = (uint32_t)s_data->meshlet_draws.size();
        const uint32_t indirect_bytes = total_draws * 12u; // sizeof(VkDrawMeshTasksIndirectCommandEXT)
        const uint32_t count_bytes    = (uint32_t)mesh_order.size() * sizeof(uint32_t);

        if (!s_data->indirect_buffer || s_data->indirect_buffer->get_size() < indirect_bytes)
            s_data->indirect_buffer = StorageBuffer::create(indirect_bytes,
                StorageBufferUsage::Dynamic | StorageBufferUsage::Indirect);

        if (!s_data->count_buffer || s_data->count_buffer->get_size() < count_bytes)
            s_data->count_buffer = StorageBuffer::create(std::max(count_bytes, 4u),
                StorageBufferUsage::Dynamic | StorageBufferUsage::Indirect);

        auto indirect_vk = reinterpret_cast<VkBuffer>(s_data->indirect_buffer->get_native_buffer());
        auto count_vk    = reinterpret_cast<VkBuffer>(s_data->count_buffer->get_native_buffer());

        uint32_t global_draw_offset = 0;

        for (uint32_t mesh_idx = 0; mesh_idx < (uint32_t)mesh_order.size(); ++mesh_idx) {
            const Mesh* mesh = mesh_order[mesh_idx];
            const auto& indices = draws_by_mesh.at(mesh);
            const uint32_t mesh_draw_count = (uint32_t)indices.size();

            HN_CORE_ASSERT(mesh && mesh->meshlet_buffers.has_value(),
                "flush_meshlet_draws: mesh has no global meshlet buffers");
            auto& bufs = const_cast<GlobalMeshletBuffers&>(*mesh->meshlet_buffers);

            // Ensure per-mesh draw_data_buffer is large enough (may reset descriptor_set on growth)
            VulkanRendererAPI::update_mesh_draw_data_binding(bufs, mesh_draw_count);
            // Ensure descriptor set exists, writing all 6 bindings including draw_data_buffer
            VulkanRendererAPI::ensure_mesh_descriptor_set(bufs);

            // Build indirect commands and draw data for this mesh's draws
            std::vector<VkDrawMeshTasksIndirectCommandEXT> indirect_cmds;
            std::vector<GPUDrawData> draw_data;
            indirect_cmds.reserve(mesh_draw_count);
            draw_data.reserve(mesh_draw_count);

            for (uint32_t local_i = 0; local_i < mesh_draw_count; ++local_i) {
                const auto& cmd = s_data->meshlet_draws[indices[local_i]];
                const auto& geo = *cmd.submesh->meshlets;
                indirect_cmds.push_back({ geo.meshlet_count, 1, 1 });
                draw_data.push_back({
                    cmd.transform,
                    geo.meshlets_offset,
                    geo.meshlet_count,
                    (int32_t)indices[local_i], // index into gpu_materials (original submission order)
                    cmd.entity_id
                });
            }

            // Upload indirect commands to shared buffer at global offset
            const uint32_t indirect_byte_off = global_draw_offset * 12u;
            const uint32_t count_byte_off    = mesh_idx * (uint32_t)sizeof(uint32_t);

            s_data->indirect_buffer->set_data(indirect_cmds.data(),
                mesh_draw_count * 12u, indirect_byte_off);
            s_data->count_buffer->set_data(&mesh_draw_count, sizeof(uint32_t), count_byte_off);

            // Upload draw data to per-mesh buffer at offset 0
            // (gl_DrawID resets to 0 for each indirect call, so draws[gl_DrawID] is correct)
            bufs.draw_data_buffer->set_data(draw_data.data(),
                mesh_draw_count * (uint32_t)sizeof(GPUDrawData), 0);

            // Bind this Mesh's descriptor set (set=1), then dispatch
            VulkanRendererAPI::submit_set1_descriptor_set(
                bufs.descriptor_set,
                pipe->get_native_pipeline_layout());

            VulkanRendererAPI::submit_mesh_tasks_indirect_count(
                indirect_vk, indirect_byte_off,
                count_vk,    count_byte_off,
                mesh_draw_count, 12u);

            s_data->stats.draw_calls++;
            global_draw_offset += mesh_draw_count;
        }

        s_data->meshlet_draws.clear();
    }

    void Renderer3D::end_scene() {
        HN_PROFILE_FUNCTION();

        if (Renderer::get_api() == RendererAPI::API::vulkan) {
            flush_batches_vulkan();
            flush_meshlet_draws();
        } else {
            HN_CORE_ASSERT(false, "Renderer3D::end_scene: only Vulkan path implemented");
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
        VulkanRendererAPI::submit_lights(lights);
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
                .material  = material,
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