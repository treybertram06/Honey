#pragma once

#include "entity.h"
#include "scene.h"
#include "Honey/core/base.h"
#include "yaml-cpp/node/node.h"

namespace Honey {

    struct EditorSceneMeta {
        glm::vec3 camera_position{0.0f};
        float camera_yaw   = 0.0f;
        float camera_pitch = 0.0f;
        float camera_fov   = 45.0f;
        float camera_near  = 0.1f;
        float camera_far   = 1000.0f;
        bool  has_camera   = false;
    };

    class SceneSerializer {
    public:
        SceneSerializer(const Ref<Scene>& scene, const EditorSceneMeta* meta = nullptr);

        void serialize(const std::filesystem::path& path);
        void serialize_entity_prefab(const Entity& entity, const std::filesystem::path& path);
        void serialize_runtime(const std::filesystem::path& path);

        bool deserialize(const std::filesystem::path& path);
        Entity deserialize_entity_node(YAML::Node& entity_node, bool generate_new_uuid = false);
        Entity deserialize_entity_prefab(const std::filesystem::path& path);
        bool deserialize_runtime(const std::filesystem::path& path);

        const EditorSceneMeta& get_loaded_editor_meta() const { return m_loaded_editor_meta; }
    private:

        struct PendingRelationship {
            Entity child;
            UUID parent_uuid;
        };

        struct PendingTransform {
            Entity entity;
            glm::vec3 translation;
            glm::vec3 rotation;
            glm::vec3 scale;
        };

        Ref<Scene> m_scene;
        std::vector<PendingRelationship> m_pending_relationships;
        std::vector<PendingTransform> m_pending_transforms;

        const EditorSceneMeta* m_editor_meta = nullptr;
        EditorSceneMeta m_loaded_editor_meta;
    };
}
