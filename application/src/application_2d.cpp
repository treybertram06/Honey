#include "application_2d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "hnpch.h"


Application2D::Application2D()
    : Layer("Application2D"),
      m_camera_controller((1.6f / 0.9f), true) {
}


void Application2D::on_attach() {
    m_chuck_texture = Honey::Texture2D::create("../../application/assets/textures/bung.png");
    m_missing_texture = Honey::Texture2D::create("../../application/assets/textures/missing.png");
    m_transparent_texture = Honey::Texture2D::create("../../application/assets/textures/transparent.png");
}

void Application2D::on_detach() {
}

void Application2D::on_update(Honey::Timestep ts) {
    HN_PROFILE_FUNCTION();

    frame_time = ts.get_millis();

    // update
    {
        HN_PROFILE_SCOPE("Application2D::camera_update");
        m_camera_controller.on_update(ts);
    }

    //profiling
    {
        HN_PROFILE_SCOPE("Application2D::framerate");
        framerate_counter.update(ts);
        framerate = framerate_counter.get_smoothed_fps();
    }

    {
        HN_PROFILE_SCOPE("Application2D::renderer_clear");
        // render
        Honey::RenderCommand::set_clear_color({0.1f, 0.1f, 0.1f, 1.0f});
        Honey::RenderCommand::clear();
    }

    {
        HN_PROFILE_SCOPE("Application2D::renderer_draw");
        Honey::Renderer2D::begin_scene(m_camera_controller.get_camera());

        //Honey::ScopedTimer timer("Renderer2D::draw_quad");
        Honey::Renderer2D::draw_quad({0.0f, 0.0f}, {1.0f, 1.0f}, m_square_color);
        Honey::Renderer2D::draw_quad({2.0f, 2.0f}, {1.0f, 1.0f}, {0.8f, 0.2f, 0.3f, 1.0f});
        Honey::Renderer2D::draw_quad({0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, m_chuck_texture, {1.0f, 0.5f, 0.5f, 1.0f}, 1.0f);
        Honey::Renderer2D::draw_quad({0.0f, -1.0f, 0.0f}, {1.5f, 1.5f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);

        Honey::Renderer2D::draw_rotated_quad({0.5f, 1.5f, 0.0f}, {0.1f, 0.1f}, 45.0f, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
        Honey::Renderer2D::draw_quad({-50.0f, -50.0f, -0.1f}, {100.0f, 100.0f}, m_missing_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1000.0f);

        Honey::Renderer2D::draw_rotated_quad({2.0f, 0.0f, 0.0f}, {0.25f, 0.25f}, glm::radians(45.0f), {0.8f, 0.2f, 0.3f, 1.0f});
        //Honey::Renderer2D::draw_quad({-1.0f, -0.33f, 0.0f}, {2.0f, 2.0f}, m_transparent_texture);

        Honey::Renderer2D::end_scene();
    }

}

void Application2D::on_imgui_render() {
    HN_PROFILE_FUNCTION();

    ImGui::Begin("Performance");
    ImGui::Text("Frame Rate: %d FPS", framerate);
    ImGui::Text("Frame Time: %.3f ms", frame_time);

    ImGui::Separator();
    ImGui::Text("Smoothed FPS: %d", framerate_counter.get_smoothed_fps());
    ImGui::End();

    ImGui::Begin("Settings");
    ImGui::ColorEdit4("Square color", glm::value_ptr(m_square_color));

    ImGui::End();
}

void Application2D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);
}