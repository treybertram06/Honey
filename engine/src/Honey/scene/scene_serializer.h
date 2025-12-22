#pragma once

#include "scene.h"
#include "Honey/core/base.h"

namespace Honey {

    class SceneSerializer {
    public:
        SceneSerializer(const Ref<Scene>& scene);

        void serialize(const std::filesystem::path& path);
        void serialize_entity_prefab(const Entity& entity, const std::filesystem::path& path);
        void serialize_runtime(const std::filesystem::path& path);

        bool deserialize(const std::filesystem::path& path);
        Entity deserialize_entity_prefab(const std::filesystem::path& path);
        bool deserialize_runtime(const std::filesystem::path& path);
    private:
        Ref<Scene> m_scene;
    };
}
