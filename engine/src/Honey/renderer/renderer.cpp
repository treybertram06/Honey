#include "hnpch.h"
#include "renderer.h"
#include "renderer_2d.h"
#include "renderer_3d.h"
#include "platform/opengl/opengl_shader.h"

namespace Honey {

    Renderer::SceneData* Renderer::m_scene_data = new Renderer::SceneData;

    void Renderer::init() {
        HN_PROFILE_FUNCTION();

        RenderCommand::init();
        Renderer2D::init();
        Renderer3D::init();
    }

    void Renderer::on_window_resize(uint32_t width, uint32_t height) {
        RenderCommand::set_viewport(0, 0, width, height);
    }

    void Renderer::begin_scene(OrthographicCamera& camera) {
        m_scene_data->view_projection_matrix = camera.get_view_projection_matrix();
    }

    void Renderer::end_scene() {
    }

    void Renderer::submit(const Ref<Shader>& shader, const Ref<VertexArray> &vertex_array, const glm::mat4& transform) {
        shader->bind();
        std::dynamic_pointer_cast<OpenGLShader>(shader)->upload_uniform_mat4("u_view_projection", m_scene_data->view_projection_matrix);
        std::dynamic_pointer_cast<OpenGLShader>(shader)->upload_uniform_mat4("u_transform", transform);

        vertex_array->bind();
        RenderCommand::draw_indexed(vertex_array);
    }

}
