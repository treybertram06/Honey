#pragma once

#include <entt/entt.hpp>

#include "Honey/core/timestep.h"

namespace Honey {

    class Scene {

    public:
        Scene();
        ~Scene();

        entt::entity create_entity(const std::string& name = "");

        //TEMP
        entt::registry& reg() { return m_registry; }

        void on_update(Timestep ts);

    private:
        entt::registry m_registry;
    };
}
