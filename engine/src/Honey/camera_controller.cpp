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
        : m_aspect_ratio(aspect_ratio), m_camera(fov, aspect_ratio, near_clip, far_clip) {
        update_camera_vectors();
    }

    void PerspectiveCameraController::on_update(Timestep ts) {
        HN_PROFILE_FUNCTION();

        float velocity = m_movement_speed * ts;

        // Keyboard movement
        if (Input::is_key_pressed(HN_KEY_W))
            m_camera_position += m_camera_front * velocity;
        if (Input::is_key_pressed(HN_KEY_S))
            m_camera_position -= m_camera_front * velocity;
        if (Input::is_key_pressed(HN_KEY_A))
            m_camera_position -= m_camera_right * velocity;
        if (Input::is_key_pressed(HN_KEY_D))
            m_camera_position += m_camera_right * velocity;
        if (Input::is_key_pressed(HN_KEY_SPACE))
            m_camera_position += m_world_up * velocity;
        if (Input::is_key_pressed(HN_KEY_LEFT_SHIFT))
            m_camera_position -= m_world_up * velocity;

        // Update camera position
        m_camera.set_position(m_camera_position);
    }

    void PerspectiveCameraController::on_event(Event& e) {
        HN_PROFILE_FUNCTION();

        EventDispatcher dispatcher(e);
        dispatcher.dispatch<MouseScrolledEvent>(HN_BIND_EVENT_FN(PerspectiveCameraController::on_mouse_scrolled));
        dispatcher.dispatch<WindowResizeEvent>(HN_BIND_EVENT_FN(PerspectiveCameraController::on_window_resize));
        dispatcher.dispatch<MouseMovedEvent>(HN_BIND_EVENT_FN(PerspectiveCameraController::on_mouse_moved));
    }

    bool PerspectiveCameraController::on_mouse_scrolled(MouseScrolledEvent& e) {
        HN_PROFILE_FUNCTION();

        float fov = m_camera.get_fov();
        fov -= e.get_yoffset() * m_zoom_sensitivity;
        fov = std::clamp(fov, 1.0f, 120.0f);
        m_camera.set_fov(fov);
        return false;
    }

    bool PerspectiveCameraController::on_window_resize(WindowResizeEvent& e) {
        HN_PROFILE_FUNCTION();

        m_aspect_ratio = (float)e.get_width() / (float)e.get_height();
        m_camera.set_aspect_ratio(m_aspect_ratio);
        return false;
    }

    bool PerspectiveCameraController::on_mouse_moved(MouseMovedEvent& e) {
        HN_PROFILE_FUNCTION();

        float mouse_x = e.get_x();
        float mouse_y = e.get_y();

        if (m_first_mouse) {
            m_last_mouse_x = mouse_x;
            m_last_mouse_y = mouse_y;
            m_first_mouse = false;
        }

        float x_offset = mouse_x - m_last_mouse_x;
        float y_offset = m_last_mouse_y - mouse_y; // Reversed since y-coordinates go from bottom to top

        m_last_mouse_x = mouse_x;
        m_last_mouse_y = mouse_y;

        // Only process mouse movement if right mouse button is pressed
        //if (Input::is_mouse_button_pressed(HN_MOUSE_BUTTON_RIGHT)) {
            x_offset *= m_mouse_sensitivity;
            y_offset *= m_mouse_sensitivity;

            m_yaw += x_offset;
            m_pitch += y_offset;

        HN_CORE_TRACE("Mouse_pos: {0}, {1}", m_yaw, m_pitch);

            // Constrain pitch to prevent screen flipping
            m_pitch = std::clamp(m_pitch, -89.0f, 89.0f);

            update_camera_vectors();
        //}

        return false;
    }

    void PerspectiveCameraController::update_camera_vectors() {
        // Calculate the new Front vector
        glm::vec3 front;
        front.x = cos(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        front.y = sin(glm::radians(m_pitch));
        front.z = sin(glm::radians(m_yaw)) * cos(glm::radians(m_pitch));
        m_camera_front = glm::normalize(front);

        // Re-calculate the Right and Up vector
        m_camera_right = glm::normalize(glm::cross(m_camera_front, m_world_up));
        m_camera_up = glm::normalize(glm::cross(m_camera_right, m_camera_front));

        // Update camera rotation
        glm::vec3 rotation;
        rotation.x = m_pitch;
        rotation.y = m_yaw;
        rotation.z = 0.0f;
        m_camera.set_rotation(rotation);
    }
}
