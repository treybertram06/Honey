#pragma once
#include "Honey/renderer/camera.h"
#include "Honey/core/uuid.h"
#include "Honey/renderer/texture.h"

#include "glm/glm.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"
#include <functional>
#include <filesystem>
#include <variant>
#include <entt/entt.hpp>

#include "box2d/id.h"
#include "Honey/renderer/mesh.h"
#include "Honey/renderer/sprite.h"


namespace Honey {

    class ScriptableEntity;
    class ScriptRegistry;

    struct IDComponent {
        UUID id;

        IDComponent() = default;
        IDComponent(const IDComponent&) = default;
        IDComponent(UUID id) : id(id) {}
    };

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

        bool dirty = false;
        bool collider_dirty = false;

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

        Ref<Sprite> sprite;
        std::filesystem::path sprite_path;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(SpriteRendererComponent&&) noexcept = default;
        SpriteRendererComponent& operator=(const SpriteRendererComponent&) = default;
        SpriteRendererComponent& operator=(SpriteRendererComponent&&) noexcept = default;
        SpriteRendererComponent(const glm::vec4& color)
            : color(color) {}

    };

    struct MeshRendererComponent {
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};

        Ref<Mesh> mesh;
        std::filesystem::path mesh_path;

        std::vector<Ref<Material>> material_overrides;

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
        MeshRendererComponent(MeshRendererComponent&&) noexcept = default;
        MeshRendererComponent& operator=(const MeshRendererComponent&) = default;
        MeshRendererComponent& operator=(MeshRendererComponent&&) noexcept = default;

        explicit MeshRendererComponent(const glm::vec4& color)
            : color(color) {}
    };

    struct CircleRendererComponent {
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        float thickness = 1.0f;
        float fade = 0.005f;

        Ref<Texture2D> texture;
        std::filesystem::path texture_path;

        CircleRendererComponent() = default;
        CircleRendererComponent(const CircleRendererComponent&) = default;
        CircleRendererComponent(CircleRendererComponent&&) noexcept = default;
        CircleRendererComponent& operator=(const CircleRendererComponent&) = default;
        CircleRendererComponent& operator=(CircleRendererComponent&&) noexcept = default;
        CircleRendererComponent(const glm::vec4& color)
            : color(color) {}

    };

    struct LineRendererComponent {
        glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f};
        float fade = 0.005f;

        Ref<Texture2D> texture;
        std::filesystem::path texture_path;

        LineRendererComponent() = default;
        LineRendererComponent(const LineRendererComponent&) = default;
        LineRendererComponent(LineRendererComponent&&) noexcept = default;
        LineRendererComponent& operator=(const LineRendererComponent&) = default;
        LineRendererComponent& operator=(LineRendererComponent&&) noexcept = default;
        LineRendererComponent(const glm::vec4& color)
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
        bool primary = false;

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
        std::string script_name = "";

        std::function<ScriptableEntity*()> instantiate_script = nullptr;
        std::function<void(NativeScriptComponent*)> destroy_script = nullptr;

        template<typename T>
        void bind() {
            instantiate_script = []() { return static_cast<ScriptableEntity*>(new T()); };
            destroy_script = destroy_script_impl;
        }

        // Bind by name using registry
        void bind_by_name(const std::string& name);

    private:
        static void destroy_script_impl(NativeScriptComponent* nsc);

    };

    struct ScriptComponent {
        std::string script_name;
        bool initialized = false;

        std::unordered_map<std::string, std::variant<
                               float,
                               bool,
                               std::string
                           >> property_overrides;

        ScriptComponent() = default;
        ScriptComponent(const ScriptComponent&) = default;
    };


    struct RelationshipComponent {
        entt::entity parent = entt::null;
        std::vector<entt::entity> children;

        RelationshipComponent() = default;
        RelationshipComponent(const RelationshipComponent&) = default;
    };


    // Physics

    struct Rigidbody2DComponent {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType body_type = BodyType::Static;
        bool fixed_rotation = false;

        void* runtime_body = nullptr;

        Rigidbody2DComponent() = default;
        Rigidbody2DComponent(const Rigidbody2DComponent&) = default;
    };

    struct BoxCollider2DComponent {
        glm::vec2 offset = {0.0f, 0.0f};
        glm::vec2 size = {0.5f, 0.5f};

        // Move into physics material at some point
        float density = 1.0f;
        float friction = 0.5f;
        float restitution = 0.0f;

        void* runtime_fixture = nullptr;
        std::vector<b2ShapeId> runtime_shapes;

        BoxCollider2DComponent() = default;
        BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
    };

    struct CircleCollider2DComponent {
        glm::vec2 offset = {0.0f, 0.0f};
        //glm::vec2 size = {0.5f, 0.5f};
        float radius = 0.5f;

        // Move into physics material at some point
        float density = 1.0f;
        float friction = 0.5f;
        float restitution = 0.0f;

        void* runtime_fixture = nullptr;
        std::vector<b2ShapeId> runtime_shapes;

        CircleCollider2DComponent() = default;
        CircleCollider2DComponent(const CircleCollider2DComponent&) = default;
    };

    struct AudioSourceComponent {
        std::filesystem::path file_path;
        float volume = 1.0f;
        float pitch = 1.0f;
        bool loop = false;
        bool play_on_scene_start = false;

        void* runtime_handle = nullptr;

        AudioSourceComponent() = default;
        AudioSourceComponent(const AudioSourceComponent&) = default;
    };

}

