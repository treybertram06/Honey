#include "hnpch.h"
#include "renderer.h"
#include "renderer_2d.h"
#include "renderer_3d.h"
#include "Honey/core/engine.h"
#include "platform/opengl/opengl_shader.h"

namespace Honey {

    Renderer::SceneData* Renderer::m_scene_data = new Renderer::SceneData;
    std::unique_ptr<ShaderCache> Renderer::m_shader_cache = nullptr;


    void Renderer::init() {
        HN_PROFILE_FUNCTION();

        auto cache_dir = std::filesystem::current_path() / "assets" / "cache" / "shaders";
        m_shader_cache = std::make_unique<ShaderCache>(cache_dir);

        RenderCommand::init();

        switch (get_api()) {
        case RendererAPI::API::opengl:
            Renderer2D::init(std::move(m_shader_cache));
            //Renderer3D::init();
            break;

        case RendererAPI::API::vulkan:
            Renderer2D::init(std::move(m_shader_cache));
            //HN_CORE_INFO("Renderer::init - Vulkan selected (skipping Renderer2D/3D init for now).");
            break;

        case RendererAPI::API::none:
        default:
            HN_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            break;
        }

    }

    void Renderer::shutdown() {
        HN_PROFILE_FUNCTION();

        if (RendererAPI::get_api() == RendererAPI::API::vulkan) {
            auto* ctx = Application::get().get_window().get_context();
            if (ctx) {
                ctx->wait_idle();
            }
        }

        switch (get_api()) {
        case RendererAPI::API::opengl:
            Renderer2D::shutdown();
            //Renderer3D::init();
            break;

        case RendererAPI::API::vulkan:
            Renderer2D::shutdown();
            //HN_CORE_INFO("Renderer::init - Vulkan selected (skipping Renderer2D/3D init for now).");
            break;

        case RendererAPI::API::none:
        default:
            HN_CORE_ASSERT(false, "RendererAPI::None is currently not supported!");
            break;
        }

        RenderCommand::shutdown();
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
        if (get_api() != RendererAPI::API::opengl) {
            HN_CORE_ASSERT(false, "Renderer::submit is OpenGL-only right now. Guard or implement Vulkan path before calling.");
            return;
        }

        shader->bind();

        auto gl_shader = std::dynamic_pointer_cast<OpenGLShader>(shader);
        HN_CORE_ASSERT(gl_shader, "Renderer::submit expected an OpenGLShader when OpenGL API is selected.");

        gl_shader->upload_uniform_mat4("u_view_projection", m_scene_data->view_projection_matrix);
        gl_shader->upload_uniform_mat4("u_transform", transform);

        vertex_array->bind();
        RenderCommand::draw_indexed(vertex_array);
    }

}
