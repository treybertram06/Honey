#pragma once

#include <entt/entt.hpp>

#include "Honey/core/timestep.h"

namespace Honey {

    class Entity;
    class Scene {

    public:
        Scene();
        ~Scene();

        Entity create_entity(const std::string& name = "");
        void destroy_entity(Entity entity);

        void on_update(Timestep ts);
        void render();

        void set_primary_camera(Entity camera_entity);
        void clear_primary_camera();

        entt::registry& get_registry() { return m_registry; }
        const entt::registry& get_registry() const { return m_registry; }

        void on_viewport_resize(uint32_t width, uint32_t height);

    private:
        entt::registry m_registry;
        friend class Entity;
        friend class SceneHierarchyPanel;

        Entity* m_primary_camera_entity = nullptr;
        bool m_has_primary_camera = false;
    };
}
