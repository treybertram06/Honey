#include "hnpch.h"
#include "entity.h"

namespace Honey {
    Entity::Entity(entt::entity handle, Scene* scene)
        : m_entity_handle(handle), m_scene(scene) {}

    bool Entity::is_valid() const {
        return m_scene && m_scene->m_registry.valid(m_entity_handle);
    }

    void Entity::destroy() {
        if (!is_valid())
            return;

        if (has_component<RelationshipComponent>()) {
            auto& rel = get_component<RelationshipComponent>();
            for (auto child_handle : rel.children) {
                Entity child = { child_handle, m_scene };
                if (child.is_valid())
                    child.destroy(); // recursive cascade
            }
        }

        // Remove from parent's children list
        if (has_parent()) {
            Entity parent = get_parent();
            if (parent.is_valid() && parent.has_component<RelationshipComponent>()) {
                auto& parent_rel = parent.get_component<RelationshipComponent>();
                parent_rel.children.erase(
                    std::remove(parent_rel.children.begin(), parent_rel.children.end(), m_entity_handle),
                    parent_rel.children.end()
                );
            }
        }

        m_scene->get_registry().destroy(m_entity_handle);
        m_entity_handle = entt::null;
    }

    bool Entity::operator==(const Entity &other) const {
        return m_entity_handle == other.m_entity_handle && m_scene == other.m_scene;
    }

    bool Entity::operator!=(const Entity &other) const {
        return !(*this == other);
    }

    static inline glm::vec3 extract_translation(const glm::mat4& m) {
        return glm::vec3(m[3]);
    }

    static inline void decompose_trs(const glm::mat4& m, glm::vec3& outT, glm::vec3& outR_euler, glm::vec3& outS) {
        // Translation
        outT = extract_translation(m);

        // Extract the basis vectors
        glm::vec3 col0 = glm::vec3(m[0]);
        glm::vec3 col1 = glm::vec3(m[1]);
        glm::vec3 col2 = glm::vec3(m[2]);

        // Scale
        outS.x = glm::length(col0);
        outS.y = glm::length(col1);
        outS.z = glm::length(col2);

        // Avoid division by zero
        glm::vec3 safeS = glm::max(outS, glm::vec3(1e-8f));

        // Remove scale from the rotation matrix
        glm::mat3 rotM;
        rotM[0] = col0 / safeS.x;
        rotM[1] = col1 / safeS.y;
        rotM[2] = col2 / safeS.z;

        // If determinant is negative, flip one axis (handle reflections)
        if (glm::determinant(rotM) < 0.0f) {
            outS.x = -outS.x;
            rotM[0] = -rotM[0];
        }

        // Convert to quaternion then to Euler (XYZ in radians, matching your current usage)
        glm::quat q = glm::quat_cast(rotM);
        outR_euler = glm::eulerAngles(q);
    }

    // Ensures the entity has a RelationshipComponent and returns it.
    static RelationshipComponent& ensure_rel(Entity e) {
        if (!e.has_component<RelationshipComponent>()) {
            return e.get_scene()->get_registry().emplace<RelationshipComponent>(e.get_handle());
        }
        return e.get_component<RelationshipComponent>();
    }

    // Removes child handle from a parent's children vector (if present)
    static void erase_child_handle(RelationshipComponent& parentRel, entt::entity child) {
        auto& vec = parentRel.children;
        auto it = std::find(vec.begin(), vec.end(), child);
        if (it != vec.end())
            vec.erase(it);
    }

    void Entity::set_parent(Entity parent) {
        HN_CORE_ASSERT(is_valid(), "set_parent() on invalid entity");
        HN_CORE_ASSERT(!parent || parent.is_valid(), "Parent is invalid");
        HN_CORE_ASSERT(!parent || parent.get_scene() == m_scene, "Parent must be in the same scene");
        HN_CORE_ASSERT(parent != *this, "Entity cannot parent itself");

        // Preserve current world transform across reparenting
        const glm::mat4 world_before = get_world_transform();

        // Ensure this has a RelationshipComponent
        auto& rel = ensure_rel(*this);

        // If we had a previous parent, unlink us from it
        if (rel.parent != entt::null) {
            Entity oldParent(rel.parent, m_scene);
            if (oldParent && oldParent.has_component<RelationshipComponent>()) {
                auto& oldRel = oldParent.get_component<RelationshipComponent>();
                erase_child_handle(oldRel, m_entity_handle);
            }
        }

        if (parent) {
            // Ensure parent rel exists and link
            auto& parentRel = ensure_rel(parent);
            rel.parent = parent.m_entity_handle;
            // Avoid duplicate child entry
            if (std::find(parentRel.children.begin(), parentRel.children.end(), m_entity_handle) == parentRel.children.end()) {
                parentRel.children.push_back(m_entity_handle);
            }
        } else {
            // Null parent
            rel.parent = entt::null;
        }

        // Recompute local transform so that world stays the same
        set_world_transform(world_before);
    }

    void Entity::remove_parent() {
        HN_CORE_ASSERT(is_valid(), "remove_parent() on invalid entity");

        if (!has_component<RelationshipComponent>())
            return;

        auto& rel = get_component<RelationshipComponent>();
        if (rel.parent == entt::null)
            return;

        // Preserve current world transform
        const glm::mat4 world_before = get_world_transform();

        // Unlink from current parent
        Entity parent(rel.parent, m_scene);
        if (parent && parent.has_component<RelationshipComponent>()) {
            auto& parentRel = parent.get_component<RelationshipComponent>();
            erase_child_handle(parentRel, m_entity_handle);
        }
        rel.parent = entt::null;

        // Apply world as new local (since no parent)
        set_world_transform(world_before);
    }

    Entity Entity::get_parent() const {
        if (!has_component<RelationshipComponent>())
            return {};
        const auto& rel = get_component<RelationshipComponent>();
        if (rel.parent == entt::null)
            return {};
        return Entity(rel.parent, m_scene);
    }

    bool Entity::has_parent() const {
        if (!has_component<RelationshipComponent>())
            return false;
        const auto& rel = get_component<RelationshipComponent>();
        return rel.parent != entt::null;
    }

    void Entity::add_child(Entity child) {
        HN_CORE_ASSERT(is_valid(), "add_child() on invalid entity");
        HN_CORE_ASSERT(child && child.is_valid(), "Child is invalid");
        HN_CORE_ASSERT(child.get_scene() == m_scene, "Child must be in same scene");
        HN_CORE_ASSERT(child != *this, "Entity cannot be its own child");

        child.set_parent(*this);
    }

    void Entity::remove_child(Entity child) {
        HN_CORE_ASSERT(is_valid(), "remove_child() on invalid entity");
        if (!child || !child.is_valid())
            return;

        if (child.has_parent() && child.get_parent() == *this) {
            child.remove_parent();
        }
    }

    std::vector<Entity> Entity::get_children() const {
        std::vector<Entity> out;
        if (!has_component<RelationshipComponent>())
            return out;

        const auto& rel = get_component<RelationshipComponent>();
        out.reserve(rel.children.size());
        for (auto handle : rel.children) {
            // Optionally: skip invalid/destroyed entities
            if (m_scene->m_registry.valid(handle))
                out.emplace_back(handle, m_scene);
        }
        return out;
    }

    bool Entity::has_children() const {
        if (!has_component<RelationshipComponent>())
            return false;
        const auto& rel = get_component<RelationshipComponent>();
        return !rel.children.empty();
    }

    // --- Transform hierarchy ------------------------------------------------------

    glm::mat4 Entity::get_world_transform() const {
        HN_CORE_ASSERT(is_valid(), "get_world_transform() on invalid entity");

        HN_CORE_ASSERT(has_component<TransformComponent>(),
                       "Entity must have TransformComponent to query transform");

        const glm::mat4 local = get_component<TransformComponent>().get_transform();

        if (has_parent()) {
            return get_parent().get_world_transform() * local;
        }
        return local;
    }

    void Entity::set_world_transform(const glm::mat4& world_transform) {
        HN_CORE_ASSERT(is_valid(), "set_world_transform() on invalid entity");
        HN_CORE_ASSERT(has_component<TransformComponent>(),
                       "Entity must have TransformComponent to set transform");

        glm::mat4 local = world_transform;
        if (has_parent()) {
            const glm::mat4 parentWorld = get_parent().get_world_transform();
            const glm::mat4 invParent   = glm::inverse(parentWorld);
            local = invParent * world_transform;
        }

        auto& tc = get_component<TransformComponent>();
        glm::vec3 T, R_euler, S;
        decompose_trs(local, T, R_euler, S);
        tc.translation = T;
        tc.rotation    = R_euler; // radians
        tc.scale       = S;
    }
}
