#pragma once
#include "glm/glm.hpp"
#include "Honey/renderer/camera.h"

namespace Honey {

    struct TagComponent {
        std::string tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : tag(tag) {}
    };


    struct TransformComponent {
        glm::mat4 transform = glm::mat4(1.0f);

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::mat4& transform)
            : transform(transform) {}

        operator glm::mat4&() { return transform; }
        operator const glm::mat4&() const { return transform; }
    };

    struct SpriteRendererComponent {
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const glm::vec4& color)
            : color(color) {}

    };

    struct CameraComponent {
    enum class ProjectionType { Orthographic, Perspective };

    // Use a unique_ptr to hold the concrete camera implementation
    std::unique_ptr<Camera> camera;
    ProjectionType projection_type = ProjectionType::Orthographic;

    // Orthographic projection parameters
    float orthographic_size = 10.0f;
    float orthographic_near = -1.0f;
    float orthographic_far = 1.0f;

    // Perspective projection parameters
    float perspective_fov = 45.0f;
    float perspective_near = 0.1f;
    float perspective_far = 1000.0f;

    // Common parameters
    bool primary = false;
    bool fixed_aspect_ratio = false;

    CameraComponent() {
        // Create default orthographic camera
        camera = std::make_unique<OrthographicCamera>(orthographic_size, 1.6f, orthographic_near, orthographic_far);
    }

    CameraComponent(const CameraComponent& other)
        : projection_type(other.projection_type),
          orthographic_size(other.orthographic_size),
          orthographic_near(other.orthographic_near),
          orthographic_far(other.orthographic_far),
          perspective_fov(other.perspective_fov),
          perspective_near(other.perspective_near),
          perspective_far(other.perspective_far),
          primary(other.primary),
          fixed_aspect_ratio(other.fixed_aspect_ratio) {

        // Deep copy the camera
        if (projection_type == ProjectionType::Orthographic) {
            camera = std::make_unique<OrthographicCamera>(orthographic_size, 1.6f, orthographic_near, orthographic_far);
        }
        // Add perspective case when you have a PerspectiveCamera class
    }

    // Move constructor
    CameraComponent(CameraComponent&& other) noexcept = default;
    CameraComponent& operator=(CameraComponent&& other) noexcept = default;

    // Helper method to update the camera's projection
    void update_projection(float aspect_ratio = 1.6f) {
        if (projection_type == ProjectionType::Orthographic) {
            camera = std::make_unique<OrthographicCamera>(orthographic_size, aspect_ratio, orthographic_near, orthographic_far);
        }
        // Add perspective case when you have a PerspectiveCamera class
    }

    // Get the camera as the base Camera type
    Camera* get_camera() { return camera.get(); }
    const Camera* get_camera() const { return camera.get(); }
};

}
