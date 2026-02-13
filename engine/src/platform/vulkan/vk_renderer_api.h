#pragma once

#include "Honey/core/base.h"
#include "Honey/renderer/renderer_api.h"
#include "vk_context.h"

#include <glm/glm.hpp>
#include <array>

namespace Honey {

    class VulkanContext;

    class VulkanRendererAPI : public RendererAPI {
    public:
        void init() override;

        void set_clear_color(const glm::vec4& color) override;
        void set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        void clear() override;

        std::string get_vendor() override;

        uint32_t get_max_texture_slots() override;

        void bind_pipeline(const Ref<Pipeline>& pipeline) override;

        void draw_indexed(const Ref<VertexArray>&, uint32_t) override;
        void draw_indexed_instanced(const Ref<VertexArray>&, uint32_t, uint32_t) override;
        static void submit_instanced_draw(
            const Ref<VertexArray>& vertex_array,
            const Ref<VertexBuffer>& instance_vb,
            uint32_t index_count,
            uint32_t instance_count,
            uint32_t instance_byte_offset = 0
        );

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
        Ref<Framebuffer> create_framebuffer(const FramebufferSpecification& spec) override;

        // Called by VulkanContext while recording the frame
        static void set_recording_context(VulkanContext* ctx);

        static void submit_camera_view_projection(const glm::mat4& view_projection);
        static bool consume_camera_view_projection(glm::mat4& out_view_projection);

        static void submit_push_constants_mat4(const glm::mat4& value);
        static void submit_push_constants(const void* data, uint32_t size, uint32_t offset = 0, VkShaderStageFlags stageFlags = VK_SHADER_STAGE_ALL);

        static constexpr uint32_t k_max_texture_slots = 32;

        static void submit_bound_textures(const std::array<void*, k_max_texture_slots>& textures, uint32_t texture_count);
        static bool consume_bound_textures(std::array<void*, k_max_texture_slots>& out_textures, uint32_t& out_texture_count);

        static void push_globals_state();
        static void pop_globals_state();

        struct GlobalsState {
            glm::mat4 viewProjection{1.0f};
            bool hasCamera = false;

            std::array<void*, k_max_texture_slots> textures{};
            uint32_t textureCount = 0;
            bool hasTextures = false;
        };

        static GlobalsState get_globals_state();
        static void set_globals_state(const GlobalsState& state);

    private:
        VkDevice m_device = nullptr;
        VkPhysicalDevice m_physical_device = nullptr;
    };

} // namespace Honey