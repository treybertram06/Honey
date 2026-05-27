#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/Collision/ContactListener.h>

namespace JPH { class PhysicsSystem; }

namespace Honey {

    class Scene;

    class JoltContactListener final : public JPH::ContactListener {
    public:
        explicit JoltContactListener(Scene* scene, JPH::PhysicsSystem* physics_system);

        JPH::ValidateResult OnContactValidate(
            const JPH::Body& bodyA, const JPH::Body& bodyB,
            JPH::RVec3Arg baseOffset,
            const JPH::CollideShapeResult& result) override;

        void OnContactAdded(const JPH::Body& bodyA, const JPH::Body& bodyB,
                            const JPH::ContactManifold& manifold,
                            JPH::ContactSettings& settings) override;

        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override;

    private:
        Scene* m_scene;
        JPH::PhysicsSystem* m_physics_system;
    };
}