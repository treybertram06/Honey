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

        void set_wireframe(bool) override;
        void set_depth_test(bool) override;
        void set_depth_write(bool) override;
        void set_blend(bool) override;
        void set_blend_for_attachment(uint32_t, bool) override;
        void set_vsync(bool mode) override;

        Ref<VertexBuffer> create_vertex_buffer(uint32_t size) override;
        Ref<VertexBuffer> create_vertex_buffer(float* vertices, uint32_t size) override;
        Ref<IndexBuffer> create_index_buffer(uint32_t* indices, uint32_t size) override;
        Ref<VertexArray> create_vertex_array() override;
        Ref<UniformBuffer> create_uniform_buffer(uint32_t size, uint32_t binding) override;
        Ref<Framebuffer> create_framebuffer(const FramebufferSpecification& spec) override;

        // Called by VulkanContext while recording the frame
        static void set_recording_context(VulkanContext* ctx);

        static bool consume_draw_request(Ref<VertexArray>& out_va, uint32_t& out_index_count, uint32_t& out_instance_count);
        static glm::vec4 consume_clear_color();
        static bool consume_clear_requested();

        static void submit_camera_view_projection(const glm::mat4& view_projection);
        static bool consume_camera_view_projection(glm::mat4& out_view_projection);

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