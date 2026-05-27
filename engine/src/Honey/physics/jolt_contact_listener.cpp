#include "hnpch.h"
#include "jolt_contact_listener.h"

#include "Honey/core/uuid.h"
#include "Honey/scene/entity.h"
#include "Honey/scripting/csharp_script_engine.h"
#include "Jolt/Physics/Body/Body.h"
#include "Jolt/Physics/Body/BodyLock.h"
#include "Jolt/Physics/PhysicsSystem.h"

namespace Honey {
    JoltContactListener::JoltContactListener(Scene* scene, JPH::PhysicsSystem* physics_system)
    : m_scene(scene), m_physics_system(physics_system) {}

    JPH::ValidateResult JoltContactListener::OnContactValidate(const JPH::Body& bodyA, const JPH::Body& bodyB,
        JPH::RVec3Arg baseOffset, const JPH::CollideShapeResult& result) {

        return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
    }

    void JoltContactListener::OnContactAdded(const JPH::Body& bodyA, const JPH::Body& bodyB,
    const JPH::ContactManifold& manifold, JPH::ContactSettings& settings) {
        UUID uuid_a = (uint64_t)bodyA.GetUserData();
        UUID uuid_b = (uint64_t)bodyB.GetUserData();

        Entity e_a = m_scene->get_entity(uuid_a);
        Entity e_b = m_scene->get_entity(uuid_b);

            if (!e_a.is_valid() || !e_b.is_valid()) return;

        auto dispatch = [](Entity receiver, Entity other) {
            if (receiver.has_component<ScriptComponent>())
                CSharpScriptEngine::on_collision_begin(receiver, other);
        };

        dispatch(e_a, e_b);
        dispatch(e_b, e_a);
    }

    void JoltContactListener::OnContactRemoved(const JPH::SubShapeIDPair& pair) {
        JPH::BodyLockRead lock_a(m_physics_system->GetBodyLockInterfaceNoLock(), pair.GetBody1ID());
        JPH::BodyLockRead lock_b(m_physics_system->GetBodyLockInterfaceNoLock(), pair.GetBody2ID());

        if (!lock_a.Succeeded() || !lock_b.Succeeded()) return;

        UUID uuid_a = (uint64_t)lock_a.GetBody().GetUserData();
        UUID uuid_b = (uint64_t)lock_b.GetBody().GetUserData();

        Entity e_a = m_scene->get_entity(uuid_a);
        Entity e_b = m_scene->get_entity(uuid_b);

        if (!e_a.is_valid() || !e_b.is_valid()) return;

        auto dispatch = [](Entity receiver, Entity other) {
            if (receiver.has_component<ScriptComponent>())
                CSharpScriptEngine::on_collision_end(receiver, other);
        };
        dispatch(e_a, e_b);
        dispatch(e_b, e_a);
    }

}
