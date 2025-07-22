#include "application_3d.h"

#include "imgui.h"
#include "glm/gtc/type_ptr.hpp"
#include "hnpch.h"


Application3D::Application3D()
    : Layer("Application3D"),
      m_camera_controller((60), (16.0f / 9.0f)) {
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
        m_framerate_counter.update(ts);
        m_framerate = m_framerate_counter.get_smoothed_fps();
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
        Honey::Renderer3D::draw_cube({0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 0.5f, 0.2f, 1.0f});
        Honey::Renderer3D::draw_cube({2.0f, 1.0f, 0.0f}, {0.5f, 2.0f, 0.5f}, {0.2f, 0.8f, 0.3f, 1.0f});
        Honey::Renderer3D::draw_cube({0.0f, 0.0f, 0.0f}, {100.0f, 0.1f, 100.0f}, {0.8f, 0.8f, 0.8f, 1.0f});


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

    // Performance Panel
    ImGui::Begin("Performance");
    ImGui::Text("Frame Rate: %d FPS", m_framerate);
    ImGui::Text("Frame Time: %.3f ms", m_frame_time);
    ImGui::Separator();
    ImGui::Text("Smoothed FPS: %d", m_framerate_counter.get_smoothed_fps());
    ImGui::End();

    // 3D Camera Control Panel
    ImGui::Begin("3D Camera Control");

    // Camera position
    glm::vec3 camera_position = m_camera_controller.get_camera().get_position();
    if (ImGui::DragFloat3("Position", glm::value_ptr(camera_position), 0.1f)) {
        m_camera_controller.get_camera().set_position(camera_position);
    }

    // Camera rotation
    glm::vec2 camera_rotation = m_camera_controller.get_camera().get_rotation();
    if (ImGui::SliderFloat2("Rotation (Yaw, Pitch)", glm::value_ptr(camera_rotation), -180.0f, 180.0f)) {
        m_camera_controller.get_camera().set_rotation(camera_rotation);
    }

    // FOV, aspect ratio, clipping planes
    ImGui::Separator();
    ImGui::Text("Projection Settings");

    float fov = m_camera_controller.get_camera().get_fov();
    if (ImGui::SliderFloat("FOV", &fov, 10.0f, 120.0f)) {
        m_camera_controller.get_camera().set_fov(fov);
    }

    float near_clip = m_camera_controller.get_camera().get_near_clip();
    float far_clip = m_camera_controller.get_camera().get_far_clip();
    if (ImGui::SliderFloat("Near Clip", &near_clip, 0.01f, 10.0f)) {
        m_camera_controller.get_camera().set_near_clip(near_clip);
    }
    if (ImGui::SliderFloat("Far Clip", &far_clip, 1.0f, 2000.0f)) {
        m_camera_controller.get_camera().set_far_clip(far_clip);
    }

    // Movement settings
    ImGui::Separator();
    ImGui::Text("Movement Settings");
    // Note: You'll need to expose these in your camera controller
    // ImGui::SliderFloat("Move Speed", &m_camera_controller.m_move_speed, 0.1f, 20.0f);
    // ImGui::SliderFloat("Mouse Sensitivity", &m_camera_controller.m_mouse_sensitivity, 0.01f, 1.0f);

    ImGui::End();

    // 3D Renderer Settings
    ImGui::Begin("3D Renderer Settings");

    // Clear color
    static glm::vec4 clear_color = {0.1f, 0.1f, 0.1f, 1.0f};
    if (ImGui::ColorEdit4("Clear Color", glm::value_ptr(clear_color))) {
        Honey::RenderCommand::set_clear_color(clear_color);
    }

    // Render modes
    static bool wireframe_mode = false;
    static bool depth_test = true;
    static bool face_culling = true;
    static bool blending = true;

    ImGui::Checkbox("Wireframe Mode", &wireframe_mode);
    ImGui::Checkbox("Depth Test", &depth_test);
    ImGui::Checkbox("Face Culling", &face_culling);
    ImGui::Checkbox("Blending", &blending);

    ImGui::End();

    // 3D Object Properties
    ImGui::Begin("3D Object Properties");

    // Cube colors and transforms
    static glm::vec4 cube1_color = {1.0f, 0.5f, 0.2f, 1.0f};
    static glm::vec4 cube2_color = {0.2f, 0.8f, 0.3f, 1.0f};
    static glm::vec4 ground_color = {0.8f, 0.8f, 0.8f, 1.0f};

    ImGui::ColorEdit4("Cube 1 Color", glm::value_ptr(cube1_color));
    ImGui::ColorEdit4("Cube 2 Color", glm::value_ptr(cube2_color));
    ImGui::ColorEdit4("Ground Color", glm::value_ptr(ground_color));

    // Transform controls
    ImGui::Separator();
    ImGui::Text("Transform Controls");

    static glm::vec3 cube1_pos = {0.0f, 1.0f, 0.0f};
    static glm::vec3 cube1_scale = {1.0f, 1.0f, 1.0f};
    static glm::vec3 cube2_pos = {2.0f, 1.0f, 0.0f};
    static glm::vec3 cube2_scale = {0.5f, 2.0f, 0.5f};

    ImGui::DragFloat3("Cube 1 Position", glm::value_ptr(cube1_pos), 0.1f);
    ImGui::DragFloat3("Cube 1 Scale", glm::value_ptr(cube1_scale), 0.1f, 0.1f, 10.0f);
    ImGui::DragFloat3("Cube 2 Position", glm::value_ptr(cube2_pos), 0.1f);
    ImGui::DragFloat3("Cube 2 Scale", glm::value_ptr(cube2_scale), 0.1f, 0.1f, 10.0f);

    ImGui::End();

    // Lighting Panel
    ImGui::Begin("Lighting");

    static glm::vec3 light_direction = {-0.2f, -1.0f, -0.3f};
    static glm::vec3 light_color = {1.0f, 1.0f, 1.0f};
    static float light_intensity = 1.0f;
    static glm::vec3 ambient_color = {0.2f, 0.2f, 0.2f};

    ImGui::DragFloat3("Light Direction", glm::value_ptr(light_direction), 0.1f, -1.0f, 1.0f);
    ImGui::ColorEdit3("Light Color", glm::value_ptr(light_color));
    ImGui::SliderFloat("Light Intensity", &light_intensity, 0.0f, 5.0f);
    ImGui::ColorEdit3("Ambient Color", glm::value_ptr(ambient_color));

    // Multiple lights
    ImGui::Separator();
    ImGui::Text("Point Lights");

    static bool enable_point_lights = true;
    ImGui::Checkbox("Enable Point Lights", &enable_point_lights);

    static glm::vec3 point_light_pos = {0.0f, 3.0f, 0.0f};
    static glm::vec3 point_light_color = {1.0f, 1.0f, 1.0f};
    static float point_light_intensity = 1.0f;

    ImGui::DragFloat3("Point Light Position", glm::value_ptr(point_light_pos), 0.1f);
    ImGui::ColorEdit3("Point Light Color", glm::value_ptr(point_light_color));
    ImGui::SliderFloat("Point Light Intensity", &point_light_intensity, 0.0f, 10.0f);

    ImGui::End();

    // Debug Panel
    ImGui::Begin("Debug");

    static bool show_wireframe = false;
    static bool show_normals = false;
    static bool show_bounding_boxes = false;
    static bool show_grid = true;
    static bool show_axes = true;

    ImGui::Checkbox("Show Wireframe", &show_wireframe);
    ImGui::Checkbox("Show Normals", &show_normals);
    ImGui::Checkbox("Show Bounding Boxes", &show_bounding_boxes);
    ImGui::Checkbox("Show Grid", &show_grid);
    ImGui::Checkbox("Show Axes", &show_axes);

    // Frustum culling
    ImGui::Separator();
    static bool frustum_culling = true;
    ImGui::Checkbox("Frustum Culling", &frustum_culling);

    // Reset buttons
    ImGui::Separator();
    if (ImGui::Button("Reset Camera")) {
        // Reset camera to default position
        m_camera_controller.get_camera().set_position({0.0f, 1.0f, -3.0f});
        m_camera_controller.get_camera().set_rotation({-90.0f, 0.0f});
    }

    ImGui::End();


}

void Application3D::on_event(Honey::Event &event) {
    m_camera_controller.on_event(event);

    Honey::EventDispatcher dispatcher(event);
    dispatcher.dispatch<Honey::KeyPressedEvent>(HN_BIND_EVENT_FN(Application3D::on_key_pressed_event));
}

bool Application3D::on_key_pressed_event(Honey::KeyPressedEvent& e) {
    if (e.get_key_code() == Honey::KeyCode::Escape)
        Honey::Application::quit();

    return false;
}