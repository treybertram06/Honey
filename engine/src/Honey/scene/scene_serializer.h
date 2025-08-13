#pragma once

#include "scene.h"
#include "Honey/core/base.h"

namespace Honey {

    class SceneSerializer {
    public:
        SceneSerializer(const Ref<Scene>& scene);

        void serialize(const std::string& path);
        void serialize_runtime(const std::string& path);

        bool deserialize(const std::string& path);
        bool deserialize_runtime(const std::string& path);
    private:
        Ref<Scene> m_scene;
    };
}
