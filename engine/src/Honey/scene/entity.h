#pragma once

#include <entt/entt.hpp>

#include "components.h"
#include "scene.h"
#include "Honey/core/log.h"

namespace Honey {

    class Entity {
    public:
        Entity() = default;
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

        void debug_print_components() const {
            HN_CORE_INFO("Entity {} components:", (uint32_t)m_entity_handle);

            // Check for common components
            if (has_component<TransformComponent>()) {
                auto& transform = get_component<TransformComponent>();
                HN_CORE_INFO("  - TransformComponent");
            }

            if (has_component<SpriteRendererComponent>()) {
                auto& sprite = get_component<SpriteRendererComponent>();
                HN_CORE_INFO("  - SpriteRendererComponent: color({}, {}, {}, {})",
                             sprite.color.r, sprite.color.g, sprite.color.b, sprite.color.a);
            }

            if (has_component<TagComponent>()) {
                auto& tag = get_component<TagComponent>();
                HN_CORE_INFO("  - TagComponent: tag='{}'", tag.tag);
            }
        }

        template<typename T>
        bool debug_has_component() const {
            return has_component<T>();
        }


        // Operators
        bool operator==(const Entity& other) const;
        bool operator!=(const Entity& other) const;
        bool operator!() const { return !is_valid(); }
        operator bool() const { return is_valid(); }
        operator entt::entity() const { return m_entity_handle; }
        operator uint32_t() const { return (uint32_t)m_entity_handle; }
        operator void*() const { return (void*)(intptr_t)(uint32_t)m_entity_handle; }


        entt::entity get_handle() const { return m_entity_handle; }
        entt::registry* get_registry() const { return &m_scene->m_registry; }

    private:
        entt::entity m_entity_handle{ entt::null };
        Scene* m_scene = nullptr;
    };
}