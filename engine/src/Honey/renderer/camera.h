#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Honey {

    class Camera {
    public:

        virtual ~Camera() = default;

        const glm::mat4& get_projection_matrix() const { return m_projection_matrix; }
        const glm::mat4& get_view_matrix() const { return m_view_matrix; }
        const glm::mat4& get_view_projection_matrix() const { return m_view_projection_matrix; }

        const glm::vec3& get_position() const { return m_position; }
        void set_position(const glm::vec3& position) { m_position = position; recalc_view_matrix(); }

        const float get_aspect_ratio() const { return m_aspect_ratio; }
        void set_aspect_ratio(const float aspect_ratio) { m_aspect_ratio = aspect_ratio; recalc_projection_matrix(); }

    protected:
        virtual void recalc_view_matrix() = 0;
        virtual void recalc_projection_matrix() = 0;

        glm::mat4 m_projection_matrix;
        glm::mat4 m_view_matrix;
        glm::mat4 m_view_projection_matrix;

        glm::vec3 m_position;

        float m_aspect_ratio;

    };

    class OrthographicCamera : public Camera {
    public:
        OrthographicCamera(float size, float aspect_ratio, float near_clip, float far_clip);

        virtual void recalc_projection_matrix() override;
        virtual void recalc_view_matrix() override;

        const float get_rotation() const { return m_rotation; }
        void set_rotation(const float rotation) { m_rotation = rotation; recalc_view_matrix(); }

        const float get_size() const { return m_size; }
        void set_size(const float size) { m_size = size; recalc_projection_matrix(); }

        void set_near_clip(float near_clip) { m_near_clip = near_clip; recalc_projection_matrix(); }
        void set_far_clip(float far_clip) { m_far_clip = far_clip; recalc_projection_matrix(); }

        float get_near_clip() const { return m_near_clip; }
        float get_far_clip() const { return m_far_clip; }


    private:
        float m_size;
        float m_rotation;

        float m_near_clip = -1.0f;
        float m_far_clip = 1.0f;
    };

    class PerspectiveCamera : public Camera {
    public:
        PerspectiveCamera(float fov_deg,
                          float aspect_ratio,
                          float near_clip = 0.1f,
                          float far_clip  = 1000.0f);

        // ── setters ─────────────────────────────────────────────────────
        void set_position(const glm::vec3& pos)   { m_position = pos;   recalc_view_matrix(); }
        void set_rotation(const glm::vec2& yaw_pitch) { m_rotation = yaw_pitch; recalc_view_matrix(); }
        void set_fov(float fov_deg)               { m_fov = fov_deg;    recalc_projection_matrix(); }
        void set_aspect_ratio(float ratio)        { m_aspect_ratio = ratio; recalc_projection_matrix(); }

        // ── getters you’ll need elsewhere ───────────────────────────────
        const glm::mat4& view_projection() const { return m_view_projection_matrix; }
        float get_fov() const                    { return m_fov; }

        glm::mat4 get_view_projection_matrix() const { return m_view_projection_matrix; }
        glm::vec3 get_position() const { return m_position; }
        glm::vec2 get_rotation() const { return m_rotation; }

        float get_near_clip() const { return m_near_clip; }
        void set_near_clip(float updated) { m_near_clip = updated; }
        float get_far_clip() { return m_far_clip; }
        void set_far_clip(float updated) { m_far_clip = updated; }

    private:
        // ── data ────────────────────────────────────────────────────────
        glm::vec2 m_rotation{0.0f};   // { yaw , pitch } in degrees
        float     m_fov;
        float     m_near_clip;
        float     m_far_clip;


        // ── helpers ─────────────────────────────────────────────────────
        void recalc_projection_matrix();
        void recalc_view_matrix();
    };
}