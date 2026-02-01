#include "hnpch.h"
#include "editor_camera.h"

#include "Honey/core/input.h"
#include "Honey/core/keycodes.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"


namespace Honey {

    EditorCamera::EditorCamera(float aspect_ratio, float fov, float near_clip, float far_clip)
        : m_fov(fov), m_near_clip(near_clip), m_far_clip(far_clip)
    {
        set_aspect_ratio(aspect_ratio);
        if (m_distance < 0.1f) m_distance = 10.0f;
        recalc_view_matrix();
    }

    void EditorCamera::on_update(Timestep /*ts*/)
    {
        if (Input::is_key_pressed(KeyCode::LeftAlt)) {
            glm::vec2 mouse{ Input::get_mouse_x(), Input::get_mouse_y() };
            glm::vec2 delta = (mouse - m_initial_mouse_position) * 0.003f;
            m_initial_mouse_position = mouse;

            if (Input::is_mouse_button_pressed(MouseButton::Right)) {
                mouse_pan(delta);
            } else if (Input::is_mouse_button_pressed(MouseButton::Left)) {
                mouse_rotate(delta);
            } else if (Input::is_mouse_button_pressed(MouseButton::Middle)) {
                mouse_zoom(delta.y);
            }
        }

        if (m_viewport_height > 0.0f) {
            float ar = m_viewport_width / m_viewport_height;
            if (std::abs(ar - get_aspect_ratio()) > 1e-6f)
                set_aspect_ratio(ar);
        }

        recalc_view_matrix();
    }

    void EditorCamera::on_event(Event& e) {
        EventDispatcher dispatcher(e);
        dispatcher.dispatch<MouseScrolledEvent>(HN_BIND_EVENT_FN(EditorCamera::on_mouse_scrolled));
    }

    glm::vec3 EditorCamera::get_up_direction() const      { return glm::rotate(get_orientation(), glm::vec3(0.0f, 1.0f, 0.0f)); }
    glm::vec3 EditorCamera::get_right_direction() const   { return glm::rotate(get_orientation(), glm::vec3(1.0f, 0.0f, 0.0f)); }
    glm::vec3 EditorCamera::get_forward_direction() const {
        return glm::rotate(get_orientation(), glm::vec3(0.0f, 0.0f, -1.0f));
    }

    glm::quat EditorCamera::get_orientation() const {
        return glm::quat(glm::vec3(-m_pitch, -m_yaw, 0.0f));
    }

    // --- Private controls ---

    bool EditorCamera::on_mouse_scrolled(MouseScrolledEvent& e) {
        mouse_zoom(e.get_yoffset() * 0.5f);
        recalc_view_matrix();
        return false;
    }

    void EditorCamera::mouse_pan(const glm::vec2& delta) {
        auto [xSpeed, ySpeed] = pan_speed();
        m_focal_point += -get_right_direction() * delta.x * xSpeed * m_distance;
        m_focal_point +=  get_up_direction()    * delta.y * ySpeed * m_distance; // was right; use up
    }

    void EditorCamera::mouse_rotate(const glm::vec2& delta) {
        float yawSign = (get_up_direction().y < 0.0f) ? -1.0f : 1.0f;
        m_yaw   += yawSign * delta.x * rotate_speed();
        m_pitch +=          delta.y * rotate_speed();
        // (optional) clamp pitch if you want: m_pitch = std::clamp(m_pitch, -1.56f, 1.56f);
    }

    void EditorCamera::mouse_zoom(float delta) {
        m_distance -= delta * zoom_speed();
        if (m_distance < 0.1f) {
            m_focal_point += get_forward_direction();
            m_distance = 0.1f;
        }
    }

    glm::vec3 EditorCamera::calculate_position() const {
        return m_focal_point - get_forward_direction() * m_distance;
    }

    std::pair<float, float> EditorCamera::pan_speed() const {
        float x = std::min(m_viewport_width  / 1000.0f, 2.4f);
        float y = std::min(m_viewport_height / 1000.0f, 2.4f);
        float xFactor = 0.0366f * (x * x) - 0.1778f * x + 0.3021f;
        float yFactor = 0.0366f * (y * y) - 0.1778f * y + 0.3021f;
        return { xFactor, yFactor };
    }

    float EditorCamera::rotate_speed() const { return 0.8f; }

    float EditorCamera::zoom_speed() const {
        float d = std::max(m_distance * 0.2f, 0.02f);
        d = std::min(d, 50.0f);
        return d;
    }

    // --- Base Camera overrides ---

    void EditorCamera::recalc_view_matrix() {
        m_position = calculate_position();
        const glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_position) * glm::toMat4(get_orientation());
        m_view_matrix = glm::inverse(transform);
        m_view_projection_matrix = m_projection_matrix * m_view_matrix;
    }

    void EditorCamera::recalc_projection_matrix() {
        // Build a standard OpenGL-style perspective projection (Y up, z in [-1, 1]).
        glm::mat4 gl_proj = glm::perspective(
            glm::radians(m_fov),
            get_aspect_ratio(),
            m_near_clip,
            m_far_clip
        );

        // Store GL-style projection; conversion to Vulkan clip space is done in the Vulkan renderer.
        m_projection_matrix     = gl_proj;
        m_view_projection_matrix = m_projection_matrix * m_view_matrix;
    }

}
