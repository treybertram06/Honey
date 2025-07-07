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
        PerspectiveCameraController(float fov, float aspect, float near_clip=0.1f, float far_clip=1000.0f);

        // call once per frame -----------------------------------------------------
        void on_update(Timestep ts);

        // feed every mouse-move event --------------------------------------------
        bool on_mouse_moved(MouseMovedEvent& e);

        // feed scroll events ------------------------------------------------------
        bool on_mouse_scrolled(MouseScrolledEvent& e);

        // window resize -----------------------------------------------------------
        bool on_window_resize(WindowResizeEvent& e);

        void on_event(Event& e);

        // expose the camera for rendering ----------------------------------------
        PerspectiveCamera& get_camera() { return m_camera; }

    private:
        PerspectiveCamera m_camera;

        // input state -------------------------------------------------------------
        float m_yaw   = -90.0f;   // start looking down –Z
        float m_pitch =  0.0f;
        glm::vec3 m_position{0.0f};

        float m_move_speed       = 5.0f;   // units per second
        float m_mouse_sensitivity= 0.1f;
        float m_zoom_sensitivity = 1.0f;

        float m_aspect_ratio;
        float m_last_mouse_x = 0.0f;
        float m_last_mouse_y = 0.0f;
        bool  m_first_mouse  = true;

        // cached basis vectors for movement --------------------------------------
        glm::vec3 m_front{0.0f, 0.0f, -1.0f};
        glm::vec3 m_right{1.0f, 0.0f, 0.0f};
        const glm::vec3 m_world_up{0.0f, 1.0f, 0.0f};

        void update_direction_vectors();
    };

}