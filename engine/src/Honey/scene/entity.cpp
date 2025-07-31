#include "hnpch.h"
#include "entity.h"

namespace Honey {

    Entity::Entity(entt::entity handle, entt::registry *registry)
        : m_entity_handle(handle), m_registry(registry) {}

    bool Entity::is_valid() const {
        return m_registry && m_registry->valid(m_entity_handle);
    }

    void Entity::destroy() {
        if (is_valid()) {
            m_registry->destroy(m_entity_handle);
            m_entity_handle = entt::null;
        }
    }

    bool Entity::operator==(const Entity &other) const {
        return m_entity_handle == other.m_entity_handle && m_registry == other.m_registry;
    }

    bool Entity::operator!=(const Entity &other) const {
        return !(*this == other);
    }
}
