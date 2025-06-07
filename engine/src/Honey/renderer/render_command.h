#pragma once

#include "renderer_api.h"

namespace Honey {

    class RenderCommand {
    public:

        inline static void init() {
            s_renderer_api->init();
        }

        inline static void set_clear_color(const glm::vec4& color) {
            s_renderer_api->set_clear_color(color);
        }

        inline static void clear() {
            s_renderer_api->clear();
        }

        inline static void draw_indexed(const Ref<VertexArray>& vertex_array) {
            s_renderer_api->draw_indexed(vertex_array);
        }

    private:
        static RendererAPI* s_renderer_api;
    };
}