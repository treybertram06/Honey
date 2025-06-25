#include "application_2d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"

Application2D::Application2D()
    : Layer("Application2D"),
    m_camera_controller((1.6f / 0.9f), true) {}


void Application2D::on_attach() {

    m_chuck_texture = Honey::Texture2D::create("../../application/assets/textures/bung.png");
    m_missing_texture = Honey::Texture2D::create("../../application/assets/textures/missing.png");
    m_transparent_texture = Honey::Texture2D::create("../../application/assets/textures/transparent.png");

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

        Honey::Renderer2D::draw_quad({0.0f, 0.0f}, {1.0f, 1.0f}, m_square_color);
        Honey::Renderer2D::draw_quad({2.0f, 2.0f}, {1.0f, 1.0f}, {0.8f, 0.2f, 0.3f, 1.0f});
        Honey::Renderer2D::draw_quad({0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
        Honey::Renderer2D::draw_quad({0.0f, 0.0f, -0.1f}, {100.0f, 100.0f}, m_missing_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1000.0f);
        Honey::Renderer2D::draw_quad({-1.0f, -0.33f, 0.0f}, {2.0f, 2.0f}, m_transparent_texture);

    /*
     * class Batch {
     * void add(Sprite) { id is assigned }
     * Sprite remove(id) { sprite with associated id is removed and Sprite is returned but does not require capture }
     * helpers()...
     * void draw_batch()
     * }
     */



        //add Sprites to the Batch
        //draw_batch();

        Honey::Renderer2D::end_scene();

}


void Application2D::on_imgui_render() {

    ImGui::Begin("Settings");
    ImGui::ColorEdit4("Square color", glm::value_ptr(m_square_color));
    ImGui::End();
}

void Application2D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);
}
