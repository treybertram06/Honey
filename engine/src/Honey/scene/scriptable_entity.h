#pragma once

#include "entity.h"

namespace Honey {
    class ScriptableEntity {
    public:

        virtual ~ScriptableEntity() = default;
        template<typename T>
        T& get_component() {
            return m_entity.get_component<T>();
        }

    protected:
        virtual void on_create() {}
        virtual void on_destroy() {}
        virtual void on_update(Timestep ts) {}

    private:
        Entity m_entity;
        friend class Scene;
    };
}