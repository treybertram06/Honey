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
    m_sprite_sheet01 = Honey::Texture2D::create("../../application/assets/test_game/textures/roguelikeSheet_transparent.png");
    m_sprite_sheet02 = Honey::Texture2D::create("../../application/assets/test_game/textures/colored-transparent.png");
    m_bush_sprite = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {14, 9},{16, 16},{1, 1},{1, 1},{0, 17});
    m_grass_sprite = Honey::SubTexture2D::create_from_coords(m_sprite_sheet01, {5, 0},{16, 16},{1, 1},{1, 1},{0, 17});
    m_player_sprite = Honey::SubTexture2D::create_from_coords(m_sprite_sheet02, {23, 7},{16, 16},{1, 1},{1, 1},{0, 17});

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
        rotation += glm::radians(15.0f) * ts;

/*
        //Honey::ScopedTimer timer("Renderer2D::draw_quad");
        Honey::Renderer2D::draw_quad({0.0f, 0.0f}, {1.0f, 1.0f}, {0.2f, 0.2f, 0.8f, 1.0f});
        Honey::Renderer2D::draw_quad({2.0f, 2.0f}, {1.0f, 1.0f}, {0.8f, 0.2f, 0.3f, 1.0f});
        Honey::Renderer2D::draw_quad({0.0f, 1.0f, 0.0f}, {0.5f, 0.5f}, m_chuck_texture, {1.0f, 0.5f, 0.5f, 1.0f}, 1.0f);
        Honey::Renderer2D::draw_quad({0.0f, -1.0f, 0.0f}, {1.5f, 1.5f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);

        Honey::Renderer2D::draw_rotated_quad({0.5f, 1.5f, 0.0f}, {3.0f, 3.0f}, rotation, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
        Honey::Renderer2D::draw_rotated_quad({0.5f, -1.5f, 0.0f}, {3.0f, 3.0f}, rotation, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
        Honey::Renderer2D::draw_quad({0.0f, 0.0f, -0.1f}, {100.0f, 100.0f}, m_missing_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 1000.0f);
*/


        Honey::Renderer2D::begin_scene(m_camera_controller.get_camera());

        //Honey::Renderer2D::draw_quad({0.0f, 0.0f, 0.0f}, {96.8f, 52.6f}, m_sprite_sheet, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
        for (int x = -50; x < 50; x++)
            for (int y = -50; y < 50; y++)
                Honey::Renderer2D::draw_quad({x*1.0f, y*1.0f, -0.1f}, {1.0f, 1.0f}, m_grass_sprite, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);

        Honey::Renderer2D::draw_quad({-0.5f, -0.5f, 0.0f}, {1.0f, 1.0f}, m_bush_sprite, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);
        Honey::Renderer2D::draw_quad({1.5f, 1.5f, 0.0f}, {1.0f, 1.0f}, m_player_sprite, {1.0f, 1.0f, 1.0f, 1.0f}, 1.0f);



        /*
        for (int x = 0; x < 1000; x++) {
            for (int y = 0; y < 1000; y++) {
                Honey::Renderer2D::draw_quad({x*0.11f, y*0.11f, 0.0f}, {0.1f, 0.1f}, m_chuck_texture, {1.0f, 1.0f, 1.0f, 1.0f}, 2.0f);
            }
        }
        */

        Honey::Renderer2D::end_scene();
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
        static glm::vec4 clear_color = {0.1f, 0.1f, 0.1f, 1.0f};
        if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(clear_color))) {
            m_clear_color = clear_color;
        }

        static bool wireframe_mode = false;
        static bool depth_test = true;
        static bool face_culling = true;
        static bool blending = true;

        if (ImGui::Checkbox("Wireframe Mode", &wireframe_mode)) {
            Honey::RenderCommand::set_wireframe(wireframe_mode);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Depth Test", &depth_test)) {
            Honey::RenderCommand::set_depth_test(depth_test);
        }

        if (ImGui::Checkbox("Face Culling", &face_culling)) {
            // Honey::RenderCommand::set_face_culling(face_culling);
        }
        ImGui::SameLine();
        if (ImGui::Checkbox("Blending", &blending)) {
            Honey::RenderCommand::set_blend(blending);
        }
    }

    // Camera Control Section
    if (ImGui::CollapsingHeader("Camera Control", ImGuiTreeNodeFlags_DefaultOpen)) {
        glm::vec3 camera_pos = m_camera_controller.get_camera().get_position();
        if (ImGui::DragFloat3("Position", glm::value_ptr(camera_pos), 0.1f)) {
            m_camera_controller.get_camera().set_position(camera_pos);
        }

        float camera_rotation = m_camera_controller.get_camera().get_rotation();
        if (ImGui::SliderFloat("Rotation", &camera_rotation, -180.0f, 180.0f)) {
            m_camera_controller.get_camera().set_rotation(camera_rotation);
        }

        float zoom = m_camera_controller.get_zoom_level();
        if (ImGui::SliderFloat("Zoom Level", &zoom, 0.1f, 10.0f)) {
            m_camera_controller.set_zoom_level(zoom);
        }

        ImGui::Separator();
        ImGui::Text("Projection Settings");

        if (ImGui::Button("Reset Camera")) {
            m_camera_controller.get_camera().set_position({0.0f, 0.0f, 0.0f});
            m_camera_controller.get_camera().set_rotation(0.0f);
            m_camera_controller.set_zoom_level(1.0f);
        }
    }

    // Object Properties Section
    if (ImGui::CollapsingHeader("Object Properties")) {
        ImGui::Text("Texture Settings");

        static float texture_tiling = 1.0f;
        ImGui::SliderFloat("Texture Tiling", &texture_tiling, 0.1f, 10.0f);

        static glm::vec4 texture_color = {1.0f, 1.0f, 1.0f, 1.0f};
        ImGui::ColorEdit4("Texture Tint", glm::value_ptr(texture_color));

        ImGui::Separator();
        ImGui::Text("Test Object Transforms");

        static glm::vec3 test_position = {0.0f, 0.0f, 0.0f};
        static glm::vec2 test_scale = {1.0f, 1.0f};
        static float test_rotation = 0.0f;

        ImGui::DragFloat3("Test Position", glm::value_ptr(test_position), 0.1f);
        ImGui::DragFloat2("Test Scale", glm::value_ptr(test_scale), 0.1f, 0.1f, 10.0f);
        ImGui::SliderFloat("Test Rotation", &test_rotation, -180.0f, 180.0f);
    }

    // Lighting Section
    if (ImGui::CollapsingHeader("Lighting")) {
        static glm::vec3 light_direction = {-0.2f, -1.0f, -0.3f};
        static glm::vec3 light_color = {1.0f, 1.0f, 1.0f};
        static float light_intensity = 1.0f;
        static glm::vec3 ambient_color = {0.2f, 0.2f, 0.2f};

        ImGui::DragFloat3("Light Direction", glm::value_ptr(light_direction), 0.1f, -1.0f, 1.0f);
        ImGui::ColorEdit3("Light Color", glm::value_ptr(light_color));
        ImGui::SliderFloat("Light Intensity", &light_intensity, 0.0f, 5.0f);
        ImGui::ColorEdit3("Ambient Color", glm::value_ptr(ambient_color));
    }

    // Post-Processing Section
    if (ImGui::CollapsingHeader("Post-Processing")) {
        static float gamma = 2.2f;
        static float exposure = 1.0f;
        static float contrast = 1.0f;
        static float brightness = 0.0f;
        static float saturation = 1.0f;

        ImGui::SliderFloat("Gamma", &gamma, 0.1f, 5.0f);
        ImGui::SliderFloat("Exposure", &exposure, 0.1f, 5.0f);
        ImGui::SliderFloat("Contrast", &contrast, 0.0f, 3.0f);
        ImGui::SliderFloat("Brightness", &brightness, -1.0f, 1.0f);
        ImGui::SliderFloat("Saturation", &saturation, 0.0f, 3.0f);
    }

    // Debug Section
    if (ImGui::CollapsingHeader("Debug")) {
        static bool show_wireframe = false;
        static bool show_normals = false;
        static bool show_bounding_boxes = false;
        static bool show_grid = false;

        HN_CORE_INFO("on_imgui_render called");
        if (ImGui::Checkbox("Show Wireframe", &show_wireframe)) {
            Honey::RenderCommand::set_wireframe(show_wireframe);
            HN_CORE_INFO("Wireframe change triggered");
        }
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

    // Texture Manager Section
    if (ImGui::CollapsingHeader("Texture Manager")) {
        ImGui::Text("Loaded Textures:");
        ImGui::BulletText("Chuck Texture: %s", m_chuck_texture ? "Loaded" : "Not Loaded");
        ImGui::BulletText("Missing Texture: %s", m_missing_texture ? "Loaded" : "Not Loaded");
        ImGui::BulletText("Transparent Texture: %s", m_sprite_sheet01 ? "Loaded" : "Not Loaded");

        if (ImGui::Button("Reload Textures")) {
            on_attach(); // Quick way to reload textures
        }
    }

    ImGui::End();




}

void Application2D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);
}