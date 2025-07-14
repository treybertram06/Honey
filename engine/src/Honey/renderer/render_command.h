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

        static uint32_t get_max_texture_slots() {
            return s_renderer_api->get_max_texture_slots();
        }


        inline static void draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count = 0) {
            s_renderer_api->draw_indexed(vertex_array, index_count);
        }

        inline static void draw_indexed_instanced(const Ref<VertexArray>& vertex_array, uint32_t index_count, uint32_t instance_count) {
            s_renderer_api->draw_indexed_instanced(vertex_array, index_count, instance_count);
        }

        inline static void set_wireframe(bool mode) {
            s_renderer_api->set_wireframe(mode);
        }

        inline static void set_depth_test(bool mode) {
            s_renderer_api->set_depth_test(mode);
        }

        inline static void set_blend(bool mode) {
            s_renderer_api->set_blend(mode);
        }

    private:
        static RendererAPI* s_renderer_api;
    };
}