#pragma once

#include <glm/glm.hpp>

#include "framebuffer.h"
#include "vertex_array.h"
#include "platform/vulkan/vk_backend.h"

namespace Honey {

    class RendererAPI {
    public:

        virtual ~RendererAPI() = default;

        enum class API {
            none = 0,
            opengl,
            vulkan
        };

        virtual void init() = 0;
        virtual void set_clear_color(const glm::vec4& color) = 0;
        virtual void set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void clear() = 0;

        virtual uint32_t get_max_texture_slots() = 0;

        virtual void draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count = 0) = 0;
        virtual void draw_indexed_instanced(const Ref<VertexArray>& vertex_array, uint32_t index_count, uint32_t instance_count) = 0;

        virtual void set_wireframe(bool mode) = 0;
        virtual void set_depth_test(bool mode) = 0;
        virtual void set_depth_write(bool mode) = 0;
        virtual void set_blend(bool mode) = 0;
        virtual void set_blend_for_attachment(uint32_t attachment, bool mode) = 0;

        virtual Ref<VertexBuffer> create_vertex_buffer(uint32_t size) = 0;
        virtual Ref<VertexBuffer> create_vertex_buffer(float* vertices, uint32_t size) = 0;
        virtual Ref<IndexBuffer> create_index_buffer(uint32_t* indices, uint32_t size) = 0;
        virtual Ref<VertexArray> create_vertex_array() = 0;
        virtual Ref<UniformBuffer> create_uniform_buffer(uint32_t size, uint32_t binding) = 0;

        virtual Ref<Framebuffer> create_framebuffer(const FramebufferSpecification &spec) = 0;

        inline static API get_api() { return s_api; }
        inline static void set_api(API api) { s_api = api; }
        static Scope<RendererAPI> create();

    private:
        static API s_api;


    };
}
