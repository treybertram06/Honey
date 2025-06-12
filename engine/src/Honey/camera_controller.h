#pragma once

#include "Honey/renderer/camera.h"
#include "Honey/core/timestep.h"

#include "Honey/events/application_event.h"
#include "Honey/events/mouse_event.h"

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
}