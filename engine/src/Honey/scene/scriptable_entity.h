#pragma once

#include "entity.h"

namespace Honey {
    class ScriptableEntity {
    public:
        template<typename T>
        T& get_component() {
            return m_entity.get_component<T>();
        }

    private:
        Entity m_entity;
        friend class Scene;
    };
}