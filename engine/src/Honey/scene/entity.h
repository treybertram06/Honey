#pragma once

#include <entt/entt.hpp>
#include "scene.h"

namespace Honey {

    class Entity {
    public:
        Entity(const Entity& other) = default;
        Entity(entt::entity handle, Scene* scene);

        // Entity operations
        bool is_valid() const;
        void destroy();

        template<typename T, typename... Args>
        T& add_component(Args&&... args) {
            HN_CORE_ASSERT(!has_component<T>(), "Entity already has component!");
            return m_scene->m_registry.emplace<T>(m_entity_handle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& get_component() {
            HN_CORE_ASSERT(has_component<T>(), "Entity does not have component!");
            return m_scene->m_registry.get<T>(m_entity_handle);
        }

        template<typename T>
        const T& get_component() const {
            HN_CORE_ASSERT(has_component<T>(), "Entity does not have component!");
            return m_scene->m_registry.get<T>(m_entity_handle);
        }

        template<typename T>
        bool has_component() const {
            return m_scene->m_registry.all_of<T>(m_entity_handle);
        }

        template<typename T>
        void remove_component() {
            HN_CORE_ASSERT(has_component<T>(), "Entity does not have component!");
            m_scene->m_registry.remove<T>(m_entity_handle);
        }

        // Operators
        bool operator==(const Entity& other) const;
        bool operator!=(const Entity& other) const;
        operator bool() const { return is_valid(); }
        operator entt::entity() const { return m_entity_handle; }

        entt::entity get_handle() const { return m_entity_handle; }
        entt::registry* get_registry() const { return &m_scene->m_registry; }

    private:
        entt::entity m_entity_handle{ entt::null };
        Scene* m_scene = nullptr;




    };
}