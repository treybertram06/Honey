#pragma once

#include <array>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Honey/core/base.h"
#include "Honey/core/settings.h"
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/gpu_types.h"
#include "Honey/renderer/material.h"
#include "Honey/renderer/mesh.h"
#include "Honey/renderer/pipeline.h"
#include "Honey/renderer/renderer_3d/renderer_3d.h"
#include "Honey/renderer/shader_cache.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_renderer_api.h"

namespace Honey::Renderer3DInternal {

    struct BatchValue {
        Ref<VertexArray> va;
        Ref<Material> material;
        std::vector<glm::mat4> transforms;
        std::vector<int32_t> entity_ids;
    };

    struct BatchKey {
        const VertexArray* va = nullptr;
        const Material* mat = nullptr;

        bool operator==(const BatchKey& other) const {
            return va == other.va && mat == other.mat;
        }
    };

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

    struct MeshletDrawCommand {
        const Submesh* submesh = nullptr;
        const Mesh* mesh = nullptr;
        Material* material = nullptr;
        glm::mat4 transform{1.0f};
        int entity_id = -1;
    };

    struct ShadowDrawEntry {
        void*    mesh_descriptor_set  = nullptr; // VkDescriptorSet (per-mesh meshlet set, set=1)
        uint32_t draw_data_base       = 0;       // base index; shader uses draw_data_base + gl_DrawID
        uint32_t draw_count           = 0;       // indirect draw count for this group
        uint32_t indirect_byte_offset = 0;       // byte offset into the frame's indirect buffer
    };

    struct ClassicShadowDrawEntry {
        VkBuffer vertex_buffer = VK_NULL_HANDLE;
        VkBuffer index_buffer  = VK_NULL_HANDLE;
        uint32_t index_count   = 0;
        uint32_t draw_count    = 0;
        uint32_t draw_data_base = 0;
    };

    struct Renderer3DData {
        static constexpr uint32_t max_textures = 1024;

        GeometryPath geometry_path = GeometryPath::Meshlet;
        std::vector<MeshletDrawCommand> meshlet_draws;
        void* meshlet_set_layout = nullptr;
        void* meshlet_desc_pool = nullptr;

        std::array<Ref<StorageBuffer>, VulkanContext::k_max_frames_in_flight> indirect_buffers{};
        std::array<Ref<StorageBuffer>, VulkanContext::k_max_frames_in_flight> count_buffers{};

        std::vector<GPUMaterial> frame_gpu_materials;
        std::unordered_map<Texture2D*, uint32_t> frame_tex_slot_map;
        std::vector<const Mesh*> frame_mesh_order;
        std::unordered_map<const Mesh*, std::vector<uint32_t>> frame_draws_by_mesh;
        std::vector<VkDrawMeshTasksIndirectCommandEXT> frame_indirect_cmds;
        std::vector<GPUDrawData> frame_draw_data;
        VulkanContext* vk_context_cache = nullptr;

        uint32_t max_texture_slots = 0;
        std::vector<Ref<Texture2D>> texture_slots;
        uint32_t texture_slot_index = 1;
        Ref<Texture2D> white_texture;

        std::vector<VulkanRendererAPI::GlobalsState> vk_globals_stack;

        Ref<ShaderCache> shader_cache;

        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_forward_pipelines;
        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_meshlet_pipelines;
        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_meshlet_gbuffer_pipelines;
        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_gbuffer_pipelines;
        std::unordered_map<PipelineVariantKey, Ref<Pipeline>, PipelineVariantKeyHash> vk_lighting_pipelines;

        glm::mat4 scene_view_proj{1.0f};
        glm::mat4 scene_view{1.0f};
        glm::vec3 scene_camera_pos{};
        LightsUBO scene_lights{};
        TiledLightingData scene_tiled_lighting{};
        Ref<Framebuffer> current_gbuffer_fb;

        Ref<Material> default_material;

        struct BatchKeyHash {
            size_t operator()(const BatchKey& key) const {
                size_t h1 = std::hash<const void*>{}(key.va);
                size_t h2 = std::hash<const void*>{}(key.mat);
                return h1 ^ (h2 + 0x9e3779b97f4a7c15ull + (h1 << 6) + (h1 >> 2));
            }
        };

        std::unordered_map<BatchKey, BatchValue, BatchKeyHash> batches;

        Ref<VertexBuffer> instance_vb;
        uint32_t instance_vb_capacity = 0;

        Renderer3D::Statistics stats;
        std::unordered_set<const void*> unique_meshes_this_frame;

        // Populated during flush_meshlet_draws; consumed by shadow.draw executor (runs after GBuffer).
        std::vector<ShadowDrawEntry> shadow_draw_list;
        std::vector<ClassicShadowDrawEntry> classic_shadow_draw_list;

        bool  directional_shadows_enabled  = false;
        float directional_shadow_distance  = 50.0f;
        float scene_camera_near            = 0.1f;
        float scene_camera_far             = 1000.0f;
        float scene_camera_fov             = 45.0f;
        float scene_camera_aspect_ratio    = 1.0f;
    };

    struct InstanceData {
        glm::mat4 model;
        int32_t entity_id;
    };
    static_assert(sizeof(InstanceData) == 68, "InstanceData size mismatch");

    extern Renderer3DData* g_renderer3d_data;

    using PipelineFactory = std::function<Ref<Pipeline>(void* rp, bool blend, bool cull_none)>;

    GPUMaterial build_gpu_material(const Material* mat,
                                   std::unordered_map<Texture2D*, uint32_t>& tex_slot_map,
                                   uint32_t& tex_slot_count);
    void ensure_instance_buffer_capacity(uint32_t required_instances);
    void flush_batches_vulkan(const PipelineFactory& get_pipeline);

    Ref<Pipeline> get_or_create_forward_pipeline(void* rp_native, bool blend, bool cull_none);
    Ref<Pipeline> get_or_create_gbuffer_pipeline(void* rp_native, bool blend, bool cull_none);
    Ref<Pipeline> get_or_create_meshlet_pipeline(void* rp_native, void* extra_layout, bool blend, bool cull_none);
    Ref<Pipeline> get_or_create_meshlet_gbuffer_pipeline(void* rp_native, void* extra_layout, bool cull_none);
    void flush_meshlet_draws();
}
