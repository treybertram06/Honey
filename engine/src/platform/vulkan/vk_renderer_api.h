#pragma once

#include "Honey/core/base.h"
#include "Honey/renderer/renderer_api.h"
#include "Honey/renderer/mesh.h"
#include "vk_context.h"
#include "vk_renderer_globals.h"

#include <glm/glm.hpp>
#include <array>

#include "../../Honey/renderer/gpu_types.h"

namespace Honey {

    class VulkanContext;

    class VulkanRendererAPI : public RendererAPI {
    public:
        void init() override;

        void set_clear_color(const glm::vec4& color) override;
        void set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void clear() override;

        std::string get_vendor() override;

        void bind_pipeline(const Ref<Pipeline>& pipeline) override;

        void draw_indexed(const Ref<VertexArray>&, uint32_t) override;
        void draw_indexed_instanced(const Ref<VertexArray>&, uint32_t, uint32_t) override;
        static void submit_mesh_tasks_draw(uint32_t group_count_x, uint32_t group_count_y = 1, uint32_t group_count_z = 1);
        static void submit_mesh_tasks_indirect_count(
            VkBuffer draw_buffer, VkDeviceSize draw_offset,
            VkBuffer count_buffer, VkDeviceSize count_offset,
            uint32_t max_draws, uint32_t stride);

        // Meshlet heap-block management (set=1, persistent region)
        static void  allocate_meshlet_heap_blocks(GlobalMeshletBuffers& bufs);
        static void  free_meshlet_heap_blocks(GlobalMeshletBuffers& bufs);
        static void  update_mesh_draw_data_binding(GlobalMeshletBuffers& bufs, uint32_t draw_count);
        static uint32_t get_meshlet_frame_slot();
        static Ref<StorageBuffer> get_mesh_draw_data_buffer(const GlobalMeshletBuffers& bufs);
        static uint32_t get_mesh_block_offset(const GlobalMeshletBuffers& bufs);
        static void  push_meshlet_pass_data(uint32_t resource_heap_base, uint32_t draw_data_base);

        void set_wireframe(bool) override;
        void set_depth_test(bool) override;
        void set_depth_write(bool) override;
        void set_blend(bool) override;
        void set_blend_for_attachment(uint32_t, bool) override;
        void set_vsync(bool mode) override;
        void set_cull_mode(CullMode mode) override;

        Ref<VertexBuffer> create_vertex_buffer(uint32_t size) override;
        Ref<VertexBuffer> create_vertex_buffer(float* vertices, uint32_t size) override;
        Ref<IndexBuffer> create_index_buffer_u32(uint32_t* indices, uint32_t size) override;
        Ref<IndexBuffer> create_index_buffer_u16(uint16_t* indices, uint32_t size) override;
        Ref<VertexArray> create_vertex_array() override;
        Ref<UniformBuffer> create_uniform_buffer(uint32_t size, uint32_t binding) override;
        Ref<StorageBuffer> create_storage_buffer(uint32_t size, StorageBufferUsage usage_flags) override;
        Ref<Framebuffer> create_framebuffer(const FramebufferSpecification& spec) override;

        // Called by VulkanContext while recording the frame
        static void set_recording_context(VulkanContext* ctx);

        // Called once by Renderer::init / Renderer::shutdown — the globals object has device
        // lifetime, unlike the per-frame recording context above.
        static void set_globals(VulkanRendererGlobals* globals);

        static void submit_camera(const CameraUBO& camera);
        static void submit_lights(const LightsUBO& lights);
        static void submit_tiled_lighting(const TiledLightingData& data);
        static void submit_materials(const std::vector<GPUMaterial>& materials, uint32_t materials_ssbo_offset);
        static void submit_shadow_matrices(const ShadowMatricesSSBO& data);
        static void submit_directional_shadows(const DirectionalShadowSSBO& data);

        static void submit_push_constants_mat4(const glm::mat4& value);
        static void submit_push_constants(const void* data, uint32_t size, uint32_t offset = 0, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);

        static void flush_globals_to_heap();

        // Pre-existing gap, preserved by the RendererGlobals migration rather than fixed silently:
        // this does not carry `materials` / `materials_ssbo_offset`, so a get/set round-trip drops
        // whatever materials the saved scope had pending.
        struct GlobalsState {
            CameraUBO cameraUBO{};
            bool hasCamera = false;

            LightsUBO lightUBO{};
            TiledLightingData tiledLighting{};

            std::vector<void*> textures;
            uint32_t textureCount = 0;
            bool hasTextures = false;

            // Debug tag to identify source of globals state (for debugging / validation purposes)
            enum class Source : uint8_t { Unknown = 0, Renderer2D, Renderer3D } source = Source::Unknown;
        };

        static GlobalsState get_globals_state();
        static void set_globals_state(const GlobalsState& state);

    private:
        VkDevice m_device = nullptr;
        VkPhysicalDevice m_physical_device = nullptr;
    };

} // namespace Honey
