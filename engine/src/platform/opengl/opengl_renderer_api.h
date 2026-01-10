#pragma once

#include "Honey/renderer/renderer_api.h"

namespace Honey {

    class OpenGLRendererAPI : public RendererAPI {

        virtual void init() override;
        virtual void set_clear_color(const glm::vec4& color) override;
        virtual void set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) override;
        virtual void clear() override;

        virtual uint32_t get_max_texture_slots() override;

        virtual void draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count = 0) override;
        virtual void draw_indexed_instanced(const Ref<VertexArray>& vertex_array, uint32_t index_count, uint32_t instance_count) override;

        virtual void set_wireframe(bool mode) override;
        virtual void set_depth_test(bool mode) override;
        virtual void set_depth_write(bool mode) override;
        virtual void set_blend(bool mode) override;
        virtual void set_blend_for_attachment(uint32_t attachment, bool mode) override;

    };
}
