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
        HN_PROFILE_FUNCTION();

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
        HN_PROFILE_FUNCTION();

        EventDispatcher dispatcher(e);
        dispatcher.dispatch<MouseScrolledEvent>(HN_BIND_EVENT_FN(OrthographicCameraController::on_mouse_scrolled));
        dispatcher.dispatch<WindowResizeEvent>(HN_BIND_EVENT_FN(OrthographicCameraController::on_window_resize));
    }

    bool OrthographicCameraController::on_mouse_scrolled(MouseScrolledEvent &e) {
        HN_PROFILE_FUNCTION();

        m_zoom_level -= e.get_yoffset() * 0.25f;
        m_zoom_level = std::max(m_zoom_level, 0.25f);
        m_camera.set_size(m_zoom_level);
        return false;
    }

    bool OrthographicCameraController::on_window_resize(WindowResizeEvent &e) {
        HN_PROFILE_FUNCTION();

        m_aspect_ratio = (float)e.get_width() / (float)e.get_height();
        m_camera.set_aspect_ratio(m_aspect_ratio);
        return false;
    }

     // ===================== PerspectiveCameraController =====================

    PerspectiveCameraController::PerspectiveCameraController(float fov, float aspect_ratio, float near_clip, float far_clip)
        : m_camera(fov, aspect_ratio, near_clip, far_clip) {}

    void PerspectiveCameraController::on_update(Timestep ts) {
        HN_PROFILE_FUNCTION();

        float velocity = m_move_speed * ts;

        if (Input::is_key_pressed(HN_KEY_W)) m_position +=  m_front * velocity;
        if (Input::is_key_pressed(HN_KEY_S)) m_position -=  m_front * velocity;
        if (Input::is_key_pressed(HN_KEY_A)) m_position +=  m_right * velocity;
        if (Input::is_key_pressed(HN_KEY_D)) m_position -=  m_right * velocity;
        if (Input::is_key_pressed(HN_KEY_SPACE))       m_position +=  m_world_up * velocity;
        if (Input::is_key_pressed(HN_KEY_LEFT_SHIFT))  m_position -=  m_world_up * velocity;

        m_camera.set_position(m_position);
    }


    void PerspectiveCameraController::on_event(Event& e) {
        HN_PROFILE_FUNCTION();

        EventDispatcher dispatcher(e);
        dispatcher.dispatch<MouseScrolledEvent>(HN_BIND_EVENT_FN(PerspectiveCameraController::on_mouse_scrolled));
        dispatcher.dispatch<WindowResizeEvent>(HN_BIND_EVENT_FN(PerspectiveCameraController::on_window_resize));
        dispatcher.dispatch<MouseMovedEvent>(HN_BIND_EVENT_FN(PerspectiveCameraController::on_mouse_moved));
        dispatcher.dispatch<KeyPressedEvent>(HN_BIND_EVENT_FN(PerspectiveCameraController::on_key_pressed_event));
    }

    bool PerspectiveCameraController::on_key_pressed_event(KeyPressedEvent &e) {
        if (e.get_key_code() == HN_KEY_L)
            Input::set_cursor_locked(m_cursor_locked = !m_cursor_locked);

        return false;
    }


    bool PerspectiveCameraController::on_mouse_scrolled(MouseScrolledEvent& e) {
        HN_PROFILE_FUNCTION();

        float fov = m_camera.get_fov() - e.get_yoffset() * m_zoom_sensitivity;
        m_camera.set_fov(std::clamp(fov, 1.0f, 120.0f));
        return false;
    }

    bool PerspectiveCameraController::on_window_resize(WindowResizeEvent& e) {
        HN_PROFILE_FUNCTION();

        m_aspect_ratio = (float)e.get_width() / (float)e.get_height();
        m_camera.set_aspect_ratio(m_aspect_ratio);
        return false;
    }

    bool PerspectiveCameraController::on_mouse_moved(MouseMovedEvent& e)
    {
        //if (!Input::is_mouse_button_pressed(HN_MOUSE_BUTTON_RIGHT)) return false;

        if (!Input::is_mouse_button_pressed(HN_MOUSE_BUTTON_RIGHT) && !m_cursor_locked)
        {
            // Keep baseline fresh so the next drag starts from 0-delta
            m_first_mouse  = true;
            m_last_mouse_x = e.get_x();
            m_last_mouse_y = e.get_y();
            return false;
        }

        // First frame after RMB went down → just seed last-pos and exit
        if (m_first_mouse)
        {
            m_first_mouse  = false;
            m_last_mouse_x = e.get_x();
            m_last_mouse_y = e.get_y();
            return false;
        }

        // ------------------------------------------------------------------
        // Calculate per-frame deltas
        float dx = e.get_x() - m_last_mouse_x;   // “x offset”
        float dy = m_last_mouse_y - e.get_y();   // invert Y so up = look up
        m_last_mouse_x = e.get_x();
        m_last_mouse_y = e.get_y();

        dx *= m_mouse_sensitivity;
        dy *= m_mouse_sensitivity;

        m_yaw   += dx;
        m_pitch += dy;
        m_pitch  = std::clamp(m_pitch, -89.0f, 89.0f);

        m_camera.set_rotation({ m_yaw, m_pitch });
        update_direction_vectors();

        return false;
    }


    void PerspectiveCameraController::update_direction_vectors() {
        m_front = glm::normalize(glm::vec3{
            cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch)),
            sin(glm::radians(m_pitch)),
            sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch))
        });
        m_right = glm::normalize(glm::cross(m_world_up, m_front));
    }



}
