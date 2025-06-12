#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Honey {

    class Camera {
    public:

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
        float m_near_clip = 0.01f;
        float m_far_clip = 1000.0f;

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

    private:
        float m_size;
        float m_rotation;
    };

    class PerspectiveCamera : public Camera {
    public:
        PerspectiveCamera(float fov, float aspect_ratio, float near_clip, float far_clip);

        virtual void recalc_projection_matrix() override;
        virtual void recalc_view_matrix() override;

        const glm::vec3& get_rotation() const { return m_rotation; }
        void set_rotation(const glm::vec3& rotation) { m_rotation = rotation; recalc_view_matrix(); }

    private:
        float m_fov;
        glm::vec3 m_rotation;
    };
}