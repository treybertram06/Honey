#include "application_3d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "hnpch.h"


Application3D::Application3D()
    : Layer("Application3D"),
      m_camera_controller((1.6f / 0.9f), true) {
}


void Application3D::on_attach() {
    m_chuck_texture = Honey::Texture2D::create("../../application/assets/textures/bung.png");
    m_missing_texture = Honey::Texture2D::create("../../application/assets/textures/missing.png");
    m_transparent_texture = Honey::Texture2D::create("../../application/assets/textures/transparent.png");
}

void Application3D::on_detach() {
}

void Application3D::on_update(Honey::Timestep ts) {
    HN_PROFILE_FUNCTION();
    // update
    {
        HN_PROFILE_SCOPE("Application3D::camera_update");
        m_camera_controller.on_update(ts);
    }

    //profiling
    {
        HN_PROFILE_SCOPE("Application3D::framerate");
        framerate_counter.update(ts);
        framerate = framerate_counter.get_smoothed_fps();
    }

    {
        HN_PROFILE_SCOPE("Application3D::renderer_clear");
        // render
        Honey::RenderCommand::set_clear_color({0.1f, 0.1f, 0.1f, 1.0f});
        Honey::RenderCommand::clear();
    }

    {
        HN_PROFILE_SCOPE("Application3D::renderer_draw");
        Honey::Renderer3D::begin_scene(m_camera_controller.get_camera());

        //Honey::ScopedTimer timer("Renderer3D::draw_quad");
        Honey::Renderer3D::draw_cube({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.5f, 0.2f, 1.0f});
        Honey::Renderer3D::draw_cube({2.0f, 0.0f, 0.0f}, {0.5f, 2.0f, 0.5f}, {0.2f, 0.8f, 0.3f, 1.0f});


        //Honey::Renderer3D::draw_quad({-1.0f, -0.33f, 0.0f}, {2.0f, 2.0f}, m_transparent_texture);

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

        Honey::Renderer3D::end_scene();
    }

}

void Application3D::on_imgui_render() {
    HN_PROFILE_FUNCTION();
    /*
    ImGui::Begin("Settings");
    ImGui::ColorEdit4("Square color", glm::value_ptr(m_square_color));

    ImGui::End();
    */
}

void Application3D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);
}