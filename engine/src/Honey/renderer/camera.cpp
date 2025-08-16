#include "hnpch.h"

#include "camera.h"

namespace Honey {




    OrthographicCamera::OrthographicCamera(float size, float aspect_ratio, float near_clip, float far_clip) {
        this->m_size = size;
        this->m_aspect_ratio = aspect_ratio;
        this->m_near_clip = near_clip;
        this->m_far_clip = far_clip;

        OrthographicCamera::recalc_projection_matrix();
        OrthographicCamera::recalc_view_matrix();
        m_view_projection_matrix = m_projection_matrix * m_view_matrix;
    }

    void OrthographicCamera::recalc_projection_matrix() {
        HN_PROFILE_FUNCTION();

        float half_height = m_size * 0.5f;
        float half_width = half_height * m_aspect_ratio;

        m_projection_matrix = glm::ortho(
            -half_width, half_width,
            -half_height, half_height,
            m_near_clip, m_far_clip
        );

        m_view_projection_matrix = m_projection_matrix * m_view_matrix;

    }

    void OrthographicCamera::recalc_view_matrix() {
        HN_PROFILE_FUNCTION();

        glm::mat4 transform = glm::translate(glm::mat4(1.0f), m_position) * glm::rotate(glm::mat4(1.0f), glm::radians(m_rotation), glm::vec3(0, 0, 1));
        //glm::mat4 transform =  glm::rotate(glm::mat4(1.0f), glm::radians(m_rotation), glm::vec3(0, 0, 1)) * glm::translate(glm::mat4(1.0f), m_position);

        m_view_matrix = glm::inverse(transform);
        m_view_projection_matrix = m_projection_matrix * m_view_matrix;
    }

    //////perspective///////////////////////////////////////

    PerspectiveCamera::PerspectiveCamera(float fov_deg, float aspect_ratio, float near_clip, float far_clip) {
        this->m_fov = fov_deg;
        this->m_aspect_ratio = aspect_ratio;
        this->m_near_clip = near_clip;
        this->m_far_clip = far_clip;

        recalc_projection_matrix();
        recalc_view_matrix();
    }

    void PerspectiveCamera::recalc_projection_matrix() {
        HN_PROFILE_FUNCTION();

        m_projection_matrix = glm::perspective(glm::radians(m_fov),
                                               m_aspect_ratio,
                                               m_near_clip,
                                               m_far_clip);
        m_view_projection_matrix = m_projection_matrix * m_view_matrix;
    }

    void Honey::PerspectiveCamera::recalc_view_matrix() {
        // Build VIEW as inverse of T * R_yaw * R_pitch (no cross products)
        // Convention: yaw around Y, pitch around X, degrees in m_rotation.{x,y}
        const float yaw   = glm::radians(m_rotation.x);
        const float pitch = glm::radians(m_rotation.y);

        glm::mat4 V(1.0f);
        // inverse rotation order is reversed and angles negated
        V = glm::rotate(V, -pitch, glm::vec3(1.0f, 0.0f, 0.0f)); // R_x(-pitch)
        V = glm::rotate(V, -yaw,   glm::vec3(0.0f, 1.0f, 0.0f)); // R_y(-yaw)
        V = glm::translate(V, -m_position);                       // T(-pos)

        m_view_matrix = V;
        m_view_projection_matrix = m_projection_matrix * m_view_matrix;
    }

}
