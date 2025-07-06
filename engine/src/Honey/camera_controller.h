#pragma once

#include "Honey/renderer/camera.h"
#include "Honey/core/timestep.h"

#include "Honey/events/application_event.h"
#include "Honey/events/mouse_event.h"
#include "Honey/core/mouse_button_codes.h"

namespace Honey {

    class OrthographicCameraController {
    public:
        OrthographicCameraController(float aspect_ratio, bool rotation);

        void on_update(Timestep ts);
        void on_event(Event& e);

        OrthographicCamera& get_camera() { return m_camera; }
        const OrthographicCamera& get_camera() const { return m_camera; }

        float get_zoom_level() { return m_zoom_level; }
        void set_zoom_level(float level) { m_zoom_level = level; }

    private:
        bool on_mouse_scrolled(MouseScrolledEvent& e);
        bool on_window_resize(WindowResizeEvent& e);

        float m_aspect_ratio;
        float m_zoom_level = 1.0f;
        OrthographicCamera m_camera;

        bool m_rotation;

        glm::vec3 m_camera_position = { 0.0f, 0.0f, 0.0f };
        float m_camera_rotation = 0.0f;
        float m_camera_translation_speed = 1.0f, m_camera_rotation_speed = 60.0f;
    };

    class PerspectiveCameraController {
    public:
        PerspectiveCameraController(float fov, float aspect_ratio, float near_clip = 0.1f, float far_clip = 1000.0f);

        void on_update(Timestep ts);
        void on_event(Event& e);

        PerspectiveCamera& get_camera() { return m_camera; }
        const PerspectiveCamera& get_camera() const { return m_camera; }

        float get_fov() const { return m_camera.get_fov(); }
        void set_fov(float fov) { m_camera.set_fov(fov); }

        // Camera controls
        void set_movement_speed(float speed) { m_movement_speed = speed; }
        void set_mouse_sensitivity(float sensitivity) { m_mouse_sensitivity = sensitivity; }
        void set_zoom_sensitivity(float sensitivity) { m_zoom_sensitivity = sensitivity; }

    private:
        bool on_mouse_scrolled(MouseScrolledEvent& e);
        bool on_window_resize(WindowResizeEvent& e);
        bool on_mouse_moved(MouseMovedEvent& e);

        void update_camera_vectors();

        float m_aspect_ratio;
        PerspectiveCamera m_camera;

        // Camera movement
        glm::vec3 m_camera_position = { 0.0f, 0.0f, 3.0f };
        glm::vec3 m_camera_front = { 0.0f, 0.0f, -1.0f };
        glm::vec3 m_camera_up = { 0.0f, 1.0f, 0.0f };
        glm::vec3 m_camera_right = { 1.0f, 0.0f, 0.0f };
        glm::vec3 m_world_up = { 0.0f, 1.0f, 0.0f };

        // Euler angles
        float m_yaw = -90.0f;   // Yaw is initialized to -90.0 degrees since a yaw of 0.0 results in a direction vector pointing to the right
        float m_pitch = 0.0f;

        // Camera options
        float m_movement_speed = 5.0f;
        float m_mouse_sensitivity = 0.1f;
        float m_zoom_sensitivity = 2.0f;

        // Mouse state
        bool m_first_mouse = true;
        float m_last_mouse_x = 0.0f;
        float m_last_mouse_y = 0.0f;
    };

}