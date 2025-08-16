#pragma once
#include "glm/glm.hpp"
#include "Honey/renderer/camera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"

namespace Honey {

    class ScriptableEntity;

    struct TagComponent {
        std::string tag;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : tag(tag) {}
    };


    struct TransformComponent {
        glm::vec3 translation = {0.0f, 0.0f, 0.0f};
        glm::vec3 rotation = {0.0f, 0.0f, 0.0f};
        glm::vec3 scale = {1.0f, 1.0f, 1.0f};

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& translation)
            : translation(translation) {}

        glm::mat4 get_transform() const {
            glm::mat4 rotation_matrix = glm::toMat4(glm::quat(rotation));

            return glm::translate(glm::mat4(1.0f), translation)
            * rotation_matrix
            * glm::scale(glm::mat4(1.0f), scale);
        }
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
              fixed_aspect_ratio(other.fixed_aspect_ratio) {

            // Deep copy the camera
            if (projection_type == ProjectionType::Orthographic) {
                camera = std::make_unique<OrthographicCamera>(orthographic_size, (1.6f / 0.9f), orthographic_near, orthographic_far);
            }
            else if (projection_type == ProjectionType::Perspective) {
                camera = std::make_unique<PerspectiveCamera>(perspective_fov, (1.6f / 0.9f), perspective_near, perspective_far);
            }
        }

        // Move constructor
        CameraComponent(CameraComponent&& other) noexcept = default;
        CameraComponent& operator=(CameraComponent&& other) noexcept = default;

        // Helper method to update the camera's projection
        void update_projection(float aspect_ratio = 1.6f) {
            if (projection_type == ProjectionType::Orthographic) {
                camera = std::make_unique<OrthographicCamera>(orthographic_size, aspect_ratio, orthographic_near, orthographic_far);
            }
            else if (projection_type == ProjectionType::Perspective) {
                camera = std::make_unique<PerspectiveCamera>(perspective_fov, aspect_ratio, perspective_near, perspective_far);
            }
        }

        // Get the camera as the base Camera type
        Camera* get_camera() { return camera.get(); }
        const Camera* get_camera() const { return camera.get(); }
    };

    struct NativeScriptComponent {
        ScriptableEntity* instance = nullptr;

        ScriptableEntity*(*instantiate_script)() = nullptr;
        void(*destroy_script)(NativeScriptComponent*) = nullptr;

        template<typename T>
        void bind() {
            instantiate_script = []() { return static_cast<ScriptableEntity*>(new T()); };
            destroy_script = destroy_script_impl;
        }

    private:
        static void destroy_script_impl(NativeScriptComponent* nsc);

    };

}

