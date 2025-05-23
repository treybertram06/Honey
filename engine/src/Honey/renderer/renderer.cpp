#include "hnpch.h"
#include "renderer.h"

namespace Honey {

    Renderer::SceneData* Renderer::m_scene_data = new Renderer::SceneData;

    void Renderer::begin_scene(OrthographicCamera& camera) {
        m_scene_data->view_projection_matrix = camera.get_view_projection_matrix();
    }

    void Renderer::end_scene() {
    }

    void Renderer::submit(const std::shared_ptr<Shader>& shader, const std::shared_ptr<VertexArray> &vertex_array) {
        shader->bind();
        shader->upload_uniform_mat4("u_view_projection", m_scene_data->view_projection_matrix);
        vertex_array->bind();
        RenderCommand::draw_indexed(vertex_array);
    }

}
