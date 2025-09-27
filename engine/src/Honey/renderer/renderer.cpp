#include "hnpch.h"
#include "renderer.h"
#include "renderer_2d.h"
#include "renderer_3d.h"
#include "platform/opengl/opengl_shader.h"

namespace Honey {

    Renderer::SceneData* Renderer::m_scene_data = new Renderer::SceneData;
    std::unique_ptr<ShaderCache> Renderer::m_shader_cache = nullptr;


    void Renderer::init() {
        HN_PROFILE_FUNCTION();

        auto cache_dir = std::filesystem::current_path() / "assets" / "cache" / "shaders";
        m_shader_cache = std::make_unique<ShaderCache>(cache_dir);

        RenderCommand::init();
        Renderer2D::init(std::move(m_shader_cache));
        Renderer3D::init();
    }

    void Renderer::shutdown() {
        HN_PROFILE_FUNCTION();

        Renderer2D::shutdown();
        Renderer3D::shutdown();
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
