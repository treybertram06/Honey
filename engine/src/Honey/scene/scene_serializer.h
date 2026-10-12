#pragma once

#include "entity.h"
#include "scene.h"
#include "Honey/core/base.h"
#include "Honey/loaders/gltf_loader.h"
#include "yaml-cpp/node/node.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <functional>

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

    struct SceneLoadIssue {
        std::string entity;     // tag (or UUID) of the offending entity, empty for file-level issues
        std::string component;  // component name, empty for entity/file-level issues
        std::string message;
        bool missing_asset = false;
    };

    struct SceneLoadReport {
        std::vector<SceneLoadIssue> issues;
        bool fatal = false;

        bool has_issues() const { return !issues.empty(); }
        std::vector<std::string> to_lines() const;
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
        const SceneLoadReport& get_load_report() const { return m_report; }
    private:
        // Runs fn; on YAML/std exceptions records an issue against the current entity and continues.
        void try_component(const char* component, const std::function<void()>& fn);
        void add_issue(std::string component, std::string message, bool missing_asset = false);
        // Records a missing-asset issue and returns false if the path doesn't exist.
        bool check_asset_exists(const char* component, const std::string& path);

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
        std::unordered_map<std::string, Ref<GltfSceneTreeAsyncHandle>> m_gltf_tree_cache;

        const EditorSceneMeta* m_editor_meta = nullptr;
        EditorSceneMeta m_loaded_editor_meta;
        SceneLoadReport m_report;
        std::string m_current_entity_label;
    };
}
