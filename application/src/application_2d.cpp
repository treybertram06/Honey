#include "application_2d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "platform/opengl/opengl_shader.h"

Application2D::Application2D()
    : Layer("Application2D"),
    m_camera_controller((1.6f / 0.9f), true) {}


void Application2D::on_attach() {



}

void Application2D::on_detach() {
}

void Application2D::on_update(Honey::Timestep ts) {

     // update
        m_camera_controller.on_update(ts);

        framerate_counter.update(ts);
        framerate = framerate_counter.get_smoothed_fps();

        //HN_TRACE("Deltatime: {0}s ({1}ms)", ts.get_seconds(), ts.get_millis());

        // render
        Honey::RenderCommand::set_clear_color({0.1f, 0.1f, 0.1f, 1.0f});
        Honey::RenderCommand::clear();

        Honey::Renderer2D::begin_scene(m_camera_controller.get_camera());

        Honey::Renderer2D::draw_quad({0.0f, 0.0f}, {1.0f, 1.0f}, {0.8f, 0.2f, 0.3f, 1.0f});

        Honey::Renderer2D::end_scene();

        // TODO: Add Shader::set_mat4, Shader::set_float4
        //std::dynamic_pointer_cast<Honey::OpenGLShader>(m_shader)->bind();
        //std::dynamic_pointer_cast<Honey::OpenGLShader>(m_shader)->upload_uniform_float3("u_color", m_square_color);
}


void Application2D::on_imgui_render() {

    ImGui::Begin("Settings");
    ImGui::ColorEdit3("Square color", glm::value_ptr(m_square_color));
    ImGui::End();
}

void Application2D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);
}
