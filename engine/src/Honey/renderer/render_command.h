#pragma once

#include "renderer_api.h"

namespace Honey {

    class RenderCommand {
    public:

        inline static void init() {
            s_renderer_api->init();
        }

        inline static void set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
            s_renderer_api->set_viewport(x, y, width, height);
        }

        inline static void set_clear_color(const glm::vec4& color) {
            s_renderer_api->set_clear_color(color);
        }

        inline static void clear() {
            s_renderer_api->clear();
        }

        inline static void draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count = 0) {
            s_renderer_api->draw_indexed(vertex_array, index_count);
        }

    private:
        static RendererAPI* s_renderer_api;
    };
}