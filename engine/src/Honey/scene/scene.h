#pragma once

#include <entt/entt.hpp>

#include "entity.h"
#include "Honey/core/timestep.h"

namespace Honey {

    class Scene {

    public:
        Scene();
        ~Scene();

        Entity create_entity(const std::string& name = "");
        void destroy_entity(Entity entity);

        void on_update(Timestep ts);
        void render();

        entt::registry& get_registry() { return m_registry; }
        const entt::registry& get_registry() const { return m_registry; }

    private:
        entt::registry m_registry;
    };
}
