#pragma once

#include "entity.h"
#include "scene.h"
#include "Honey/core/base.h"
#include "yaml-cpp/node/node.h"

namespace Honey {

    //namespace YAML { class Node; }

    class SceneSerializer {
    public:
        SceneSerializer(const Ref<Scene>& scene);

        void serialize(const std::filesystem::path& path);
        void serialize_entity_prefab(const Entity& entity, const std::filesystem::path& path);
        void serialize_runtime(const std::filesystem::path& path);

        bool deserialize(const std::filesystem::path& path);
        Entity deserialize_entity_node(YAML::Node& entity_node);
        Entity deserialize_entity_prefab(const std::filesystem::path& path);
        bool deserialize_runtime(const std::filesystem::path& path);
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
    };
}
