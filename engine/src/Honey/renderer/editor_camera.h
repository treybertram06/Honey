#pragma once

#include "camera.h"
#include "Honey/core/timestep.h"
#include "Honey/events/event.h"
#include "Honey/events/mouse_event.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <utility>

namespace Honey {

    class EditorCamera : public Camera {
    public:
        EditorCamera() = default;
        EditorCamera(float aspect_ratio, float fov, float near_clip, float far_clip);

        void on_update(Timestep ts);
        void on_event(Event& e);

        // Distance from camera to focal point (orbit radius)
        float get_distance() const { return m_distance; }
        void  set_distance(float distance) { m_distance = distance; }

        void set_viewport_size(uint32_t width, uint32_t height) { m_viewport_width = width; m_viewport_height = height; }

        // Direction/orientation helpers
        glm::vec3 get_up_direction() const;
        glm::vec3 get_right_direction() const;
        glm::vec3 get_forward_direction() const;
        glm::quat get_orientation() const;

        float get_pitch() const { return m_pitch; }
        float get_yaw()   const { return m_yaw;  }

    protected:
        // Keep base Camera’s matrices in sync
        void recalc_view_matrix() override;
        void recalc_projection_matrix() override;

    private:
        bool on_mouse_scrolled(MouseScrolledEvent& e);

        void mouse_pan(const glm::vec2& delta);
        void mouse_rotate(const glm::vec2& delta);
        void mouse_zoom(float delta);

        glm::vec3 calculate_position() const;

        std::pair<float, float> pan_speed() const;
        float rotate_speed() const;
        float zoom_speed() const;

        // Lens / frustum
        float m_fov = 45.0f;
        float m_near_clip = 0.1f;
        float m_far_clip  = 1000.0f;

        // Orbit controls
        glm::vec3 m_focal_point { 0.0f, 0.0f, 0.0f };
        glm::vec2 m_initial_mouse_position { 0.0f, 0.0f };

        float m_distance = 10.0f;     // start a bit back from origin
        float m_pitch    = 0.0f;      // radians
        float m_yaw      = 0.0f;      // radians

        // Viewport (used for pan speed & aspect)
        float m_viewport_width  = 1680.0f;
        float m_viewport_height = 720.0f;
    };

} // namespace Honey
