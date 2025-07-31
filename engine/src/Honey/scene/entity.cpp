#include "hnpch.h"
#include "entity.h"

namespace Honey {

    Entity::Entity(entt::entity handle, Scene* scene)
        : m_entity_handle(handle), m_scene(scene) {}

    bool Entity::is_valid() const {
        return m_scene && m_scene->m_registry.valid(m_entity_handle);
    }

    void Entity::destroy() {
        if (is_valid()) {
            m_scene->m_registry.destroy(m_entity_handle);
            m_entity_handle = entt::null;
        }
    }

    bool Entity::operator==(const Entity &other) const {
        return m_entity_handle == other.m_entity_handle && m_scene == other.m_scene;
    }

    bool Entity::operator!=(const Entity &other) const {
        return !(*this == other);
    }
}
