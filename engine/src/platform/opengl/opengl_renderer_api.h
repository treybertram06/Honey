#pragma once

#include "Honey/renderer/renderer_api.h"

namespace Honey {

    class OpenGLRendererAPI : public RendererAPI {

        virtual void init() override;
        virtual void set_clear_color(const glm::vec4& color) override;
        virtual void set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        virtual void clear() override;

        virtual std::string get_vendor() override;

        virtual uint32_t get_max_texture_slots() override;

        virtual void bind_pipeline(const Ref<Pipeline>& pipeline) override;

        virtual void draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count = 0) override;
        virtual void draw_indexed_instanced(const Ref<VertexArray>& vertex_array, uint32_t index_count, uint32_t instance_count) override;

        virtual void set_wireframe(bool mode) override;
        virtual void set_depth_test(bool mode) override;
        virtual void set_depth_write(bool mode) override;
        virtual void set_blend(bool mode) override;
        virtual void set_blend_for_attachment(uint32_t attachment, bool mode) override;
        virtual void set_vsync(bool mode) override;

        virtual Ref<VertexBuffer> create_vertex_buffer(uint32_t size) override;
        virtual Ref<VertexBuffer> create_vertex_buffer(float* vertices, uint32_t size) override;
        virtual Ref<IndexBuffer> create_index_buffer(uint32_t* indices, uint32_t size) override;
        virtual Ref<VertexArray> create_vertex_array() override;
        virtual Ref<UniformBuffer> create_uniform_buffer(uint32_t size, uint32_t binding) override;
        virtual Ref<Framebuffer> create_framebuffer(const FramebufferSpecification& spec) override;

    };
}
