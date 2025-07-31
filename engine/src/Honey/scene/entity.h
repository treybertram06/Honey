#pragma once

#include <entt/entt.hpp>

namespace Honey {

    class Entity {
    public:
        Entity() = default;
        Entity(entt::entity handle, entt::registry* registry);

        template<typename T, typename... Args>
        T& add_component(Args&&... args);

        template<typename T>
        T& get_component();

        template<typename T>
        const T& get_component() const;

        template<typename T>
        bool has_component() const;

        template<typename T>
        void remove_component();

        // Entity operations
        bool is_valid() const;
        void destroy();

        // Operators
        bool operator==(const Entity& other) const;
        bool operator!=(const Entity& other) const;
        operator bool() const { return is_valid(); }
        operator entt::entity() const { return m_entity_handle; }

    private:
        entt::entity m_entity_handle{ entt::null };
        entt::registry* m_registry = nullptr;

        template<typename T, typename... Args>
        T& Entity::add_component(Args&&... args) {
            HN_CORE_ASSERT(!has_component<T>(), "Entity already has component!");
            return m_registry->emplace<T>(m_entity_handle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& Entity::get_component() {
            HN_CORE_ASSERT(has_component<T>(), "Entity does not have component!");
            return m_registry->get<T>(m_entity_handle);
        }

        template<typename T>
        const T& Entity::get_component() const {
            HN_CORE_ASSERT(has_component<T>(), "Entity does not have component!");
            return m_registry->get<T>(m_entity_handle);
        }

        template<typename T>
        bool Entity::has_component() const {
            return m_registry->all_of<T>(m_entity_handle);
        }

        template<typename T>
        void Entity::remove_component() {
            HN_CORE_ASSERT(has_component<T>(), "Entity does not have component!");
            m_registry->remove<T>(m_entity_handle);
        }


    };
}