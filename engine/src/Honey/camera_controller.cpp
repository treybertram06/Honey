#include "hnpch.h"
#include "camera_controller.h"

#include "Honey/core/input.h"
#include "Honey/core/keycodes.h"

namespace Honey {

    OrthographicCameraController::OrthographicCameraController(float aspect_ratio, bool rotation = false)
        : m_aspect_ratio(aspect_ratio),
            m_camera(m_zoom_level, aspect_ratio, -1.0f, 1.0f), m_rotation(rotation) {
    }

    void OrthographicCameraController::on_update(Timestep ts) {
        if (Input::is_key_pressed(HN_KEY_A))
            m_camera_position.x -= m_camera_translation_speed * ts;
        if (Input::is_key_pressed(HN_KEY_D))
            m_camera_position.x += m_camera_translation_speed * ts;
        if (Input::is_key_pressed(HN_KEY_W))
            m_camera_position.y += m_camera_translation_speed * ts;
        if (Input::is_key_pressed(HN_KEY_S))
            m_camera_position.y -= m_camera_translation_speed * ts;
        if (m_rotation) {
            if (Input::is_key_pressed(HN_KEY_Q))
                m_camera_rotation += m_camera_rotation_speed * ts;
            if (Input::is_key_pressed(HN_KEY_E))
                m_camera_rotation -= m_camera_rotation_speed * ts;
            m_camera.set_rotation(m_camera_rotation);
        }

        m_camera.set_position(m_camera_position);

        //makes camera speed up when zoomed out
        m_camera_translation_speed = m_zoom_level;
    }

    void OrthographicCameraController::on_event(Event &e) {
        EventDispatcher dispatcher(e);
        dispatcher.dispatch<MouseScrolledEvent>(HN_BIND_EVENT_FN(OrthographicCameraController::on_mouse_scrolled));
        dispatcher.dispatch<WindowResizeEvent>(HN_BIND_EVENT_FN(OrthographicCameraController::on_window_resize));
    }

    bool OrthographicCameraController::on_mouse_scrolled(MouseScrolledEvent &e) {
        m_zoom_level -= e.get_yoffset() * 0.25f;
        m_zoom_level = std::max(m_zoom_level, 0.25f);
        m_camera.set_size(m_zoom_level);
        return false;
    }

    bool OrthographicCameraController::on_window_resize(WindowResizeEvent &e) {
        m_aspect_ratio = (float)e.get_width() / (float)e.get_height();
        m_camera.set_aspect_ratio(m_aspect_ratio);
        return false;
    }

}
