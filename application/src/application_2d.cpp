#include "application_2d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "hnpch.h"
#include "Honey/core/settings.h"

static const std::filesystem::path asset_root = ASSET_ROOT;

Application2D::Application2D()
    : Layer("Application2D"),
      m_camera_controller((1.6f / 0.9f), true) {
}


void Application2D::on_attach() {
    auto texture_path_prefix = asset_root / "textures";
    m_chuck_texture = Honey::Texture2D::create(texture_path_prefix / "bung.png");
/*
    m_missing_texture = Honey::Texture2D::create(texture_path_prefix / "missing.png");
    m_sprite_sheet01 = Honey::Texture2D::create(asset_root / "test_game" / "textures"/ "roguelikeSheet_transparent.png");
    m_sprite_sheet02 = Honey::Texture2D::create(asset_root / "test_game" / "textures"/ "colored-transparent.png");
    m_bush_sprite = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {14, 9},{16, 16},{1, 1},{1, 1},{0, 17});
    s_texture_map['d'] = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {5, 0},{16, 16},{1, 1},{1, 1},{0, 17});
    s_texture_map['w'] = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {3, 1},{16, 16},{1, 1},{1, 1},{0, 17});
    m_player_sprite = Honey::SubTexture2D::create_from_coords(m_sprite_sheet02, {23, 7},{16, 16},{1, 1},{1, 1},{0, 17});
    m_map_width = s_map_width;
    m_map_height = strlen(s_map_tiles) / m_map_width;
*/
    m_camera_controller.set_zoom_level(10.0f);
}

void Application2D::on_detach() {
}

void Application2D::on_update(Honey::Timestep ts) {
    HN_PROFILE_FUNCTION();

    m_frame_time = ts.get_millis();

    // update
    {
        HN_PROFILE_SCOPE("Application2D::camera_update");
        m_camera_controller.on_update(ts);
    }

    //profiling
    {
        HN_PROFILE_SCOPE("Application2D::framerate");
        m_framerate_counter.update(ts);
        m_framerate = m_framerate_counter.get_smoothed_fps();
    }

    {
        HN_PROFILE_SCOPE("Application2D::renderer_clear");
        // render
        Honey::RenderCommand::set_clear_color(m_clear_color);
        Honey::RenderCommand::clear();
    }

    {
        HN_PROFILE_SCOPE("Application2D::renderer_draw");
        Honey::Renderer2D::begin_scene(m_camera_controller.get_camera());

        static float rotation = 0.0f;
        rotation += glm::radians(30.0f) * ts;


        Honey::Renderer2D::draw_rotated_quad({-0.5f, 0.0f}, {1.0f, 2.5f}, rotation * 2.0f, {0.2f, 0.2f, 0.8f, 1.0f});
        Honey::Renderer2D::draw_rotated_quad({1.5f, 0.3f}, {1.0f, 0.5f}, rotation, {1.0f, 0.0f, 0.0f, 1.0f});
        Honey::Renderer2D::draw_quad({3.0f, 3.0f, 0.0f}, {2.0f, 2.0f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
        /*
        Honey::Renderer2D::draw_quad({2.0f, 2.0f}, {1.0f, 1.0f}, {0.8f, 0.2f, 0.3f, 1.0f});
        Honey::Renderer2D::draw_quad({0.0f, -1.0f, 0.0f}, {1.5f, 1.5f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);

        //Honey::Renderer2D::draw_rotated_quad({0.5f, 1.5f, 0.0f}, {3.0f, 3.0f}, rotation, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
        //Honey::Renderer2D::draw_rotated_quad({0.5f, -1.5f, 0.0f}, {3.0f, 3.0f}, 0.0f, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
        Honey::Renderer2D::draw_quad({0.0f, 0.0f, -0.1f}, {100.0f, 100.0f}, m_missing_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1000.0f);
        */

/*
        Honey::Renderer2D::begin_scene(m_camera_controller.get_camera());

        //Honey::Renderer2D::draw_quad({0.0f, 0.0f, 0.0f}, {96.8f, 52.6f}, m_sprite_sheet, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
        for (uint32_t y = 0; y < m_map_height; y++) {
            for (uint32_t x = 0; x < m_map_width; x++) {
                char tile_type = s_map_tiles[x + y * m_map_width];
                if (s_texture_map.find(tile_type) != s_texture_map.end()) {
                    auto texture = s_texture_map[tile_type];
                    Honey::Renderer2D::draw_quad({(float)x - (m_map_width / 2), (float)y - (m_map_height / 2), 0.0f}, {1.0f, 1.0f}, texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
                } else {
                    auto texture = m_missing_texture;
                    Honey::Renderer2D::draw_quad({(float)x - (m_map_width / 2), (float)y - (m_map_height / 2), 0.0f}, {1.0f, 1.0f}, texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
                }
            }
        }
        */

        //Honey::Renderer2D::draw_quad({-0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, m_bush_sprite, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
        //Honey::Renderer2D::draw_quad({1.5f, 1.5f, 0.0f}, {1.0f, 1.0f}, m_water_sprite, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);



        /*
        for (int x = 0; x < 1000; x++) {
            for (int y = 0; y < 1000; y++) {
                Honey::Renderer2D::draw_quad({x*0.11f, y*0.11f, 0.0f}, {0.1f, 0.1f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
            }
        }
        */

        Honey::Renderer2D::quad_end_scene();
    }

}

void Application2D::on_imgui_render() {
    HN_PROFILE_FUNCTION();

    ImGui::Begin("Renderer Debug Panel");

        // Performance Section
        if (ImGui::CollapsingHeader("Performance", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Frame Rate: %d FPS", m_framerate);
            ImGui::Text("Frame Time: %.3f ms", m_frame_time);
            ImGui::Text("Smoothed FPS: %d", m_framerate_counter.get_smoothed_fps());

            ImGui::Separator();
            auto stats = Honey::Renderer2D::get_stats();
            ImGui::Text("Draw Calls: %d", stats.draw_calls);
            ImGui::Text("Quads: %d", stats.quad_count);
            ImGui::Text("Vertices: %d", stats.get_total_vertex_count());
            ImGui::Text("Indices: %d", stats.get_total_index_count());

            if (ImGui::Button("Reset Statistics")) {
                Honey::Renderer2D::reset_stats();
            }
        }

        // Renderer Settings Section
        if (ImGui::CollapsingHeader("Renderer Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& renderer = Honey::get_settings().renderer;

            if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(renderer.clear_color))) {
                m_clear_color = renderer.clear_color;
            }

            if (ImGui::Checkbox("Wireframe Mode", &renderer.wireframe)) {
                Honey::RenderCommand::set_wireframe(renderer.wireframe);
            }

            if (ImGui::Checkbox("Depth Test", &renderer.depth_test)) {
                Honey::RenderCommand::set_depth_test(renderer.depth_test);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Depth Write", &renderer.depth_write)) {
                Honey::RenderCommand::set_depth_write(renderer.depth_write);
            }

            if (ImGui::Checkbox("Face Culling", &renderer.face_culling)) {
                // RenderCommand::set_face_culling(renderer.face_culling);
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Blending", &renderer.blending)) {
                Honey::RenderCommand::set_blend(renderer.blending);
            }


        }
/*
        // Debug Section
        if (ImGui::CollapsingHeader("Debug")) {
            static bool show_normals = false;
            static bool show_bounding_boxes = false;
            static bool show_grid = false;

            ImGui::SameLine();
            ImGui::Checkbox("Show Normals", &show_normals);

            ImGui::Checkbox("Show Bounding Boxes", &show_bounding_boxes);
            ImGui::SameLine();
            ImGui::Checkbox("Show Grid", &show_grid);

            ImGui::Separator();
            ImGui::Text("Renderer Info");
            ImGui::Text("API: OpenGL"); // You can make this dynamic
            ImGui::Text("Version: 4.6"); // You can query this from OpenGL
        }
*/
        ImGui::End();




}

void Application2D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);
}