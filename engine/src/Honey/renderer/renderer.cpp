#include "hnpch.h"
#include "renderer.h"

namespace Honey {

    void Renderer::begin_scene() {
    }

    void Renderer::end_scene() {
    }

    void Renderer::submit(const std::shared_ptr<VertexArray> &vertex_array) {
        vertex_array->bind();
        RenderCommand::draw_indexed(vertex_array);
    }

}
