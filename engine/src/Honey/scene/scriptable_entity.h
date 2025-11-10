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

        Entity get_entity_by_tag(std::string_view tag) {
            auto* reg = m_entity.get_registry();
            auto view = reg->view<TagComponent>();

            for (auto e : view) {
                const auto& t = view.get<TagComponent>(e);
                if (t.tag == tag) {
                    // Reconstruct the Entity wrapper
                    return Entity{ e, m_entity.get_scene() };
                }
            }

            HN_CORE_ASSERT(false, "Entity with tag '{}' not found!", tag);
            return {}; // invalid Entity
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