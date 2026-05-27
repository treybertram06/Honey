#include "hnpch.h"
#include "physics_engine_3d.h"

#include "jolt_contact_listener.h"
#include "jolt_job_system.h"
#include "Honey/core/settings.h"
#include "Jolt/RegisterTypes.h"
#include "Jolt/Core/Factory.h"
#include "Jolt/Physics/Body/BodyCreationSettings.h"
#include "Jolt/Physics/Collision/Shape/BoxShape.h"
#include "Jolt/Physics/Collision/Shape/CapsuleShape.h"
#include "Jolt/Physics/Collision/Shape/SphereShape.h"

static void jolt_trace(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    HN_WARN("[Jolt] {0}", buf);
}

namespace Honey {

    void PhysicsEngine3D::init() {
        JPH::RegisterDefaultAllocator();
        JPH::Trace = jolt_trace;

        JPH::Factory::sInstance = new JPH::Factory;
        JPH::RegisterTypes();
    }

    void PhysicsEngine3D::shutdown() {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }

    void PhysicsEngine3D::on_scene_start(Scene* scene) {
        m_scene = scene;
        m_temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024); // 32 MB
        m_job_system = std::make_unique<JoltJobSystem>();
        m_system = std::make_unique<JPH::PhysicsSystem>();
        m_system->Init(
            k_max_bodies, k_num_body_mutexes,
            k_max_body_pairs, k_max_contact_constraints,
            m_bp_layer_interface,
            m_obj_vs_bp_filter,
            m_obj_vs_obj_filter
        );

        m_contact_listener = std::make_unique<JoltContactListener>(scene, m_system.get());
        m_system->SetContactListener(m_contact_listener.get());

        // Create a body for every entity that already has a RigidbodyComponent
        auto view = scene->get_registry().view<RigidbodyComponent>();
        for (auto e : view)
            create_body(Entity{e, scene});

    }

    void PhysicsEngine3D::on_scene_stop() {
        m_system.reset();
        m_job_system.reset();
        m_temp_allocator.reset();
        m_scene = nullptr;
    }

    void PhysicsEngine3D::step(float dt) {
        if (!m_system) return;
        auto& settings = Settings::get().physics;
        m_system->Update(dt, settings.substeps, m_temp_allocator.get(), m_job_system.get());
    }

    JPH::BodyID PhysicsEngine3D::create_body(Entity entity) {
        auto& tc = entity.get_component<TransformComponent>();
        auto& rb = entity.get_component<RigidbodyComponent>();

        JPH::EMotionType motion;
        JPH::ObjectLayer layer;
        switch (rb.body_type) {
        case RigidbodyComponent::BodyType::Static:
            motion = JPH::EMotionType::Static;
            layer  = Layers::NON_MOVING;
            break;
        case RigidbodyComponent::BodyType::Kinematic:
            motion = JPH::EMotionType::Kinematic;
            layer  = Layers::MOVING;
            break;
        default: // Dynamic
            motion = JPH::EMotionType::Dynamic;
            layer  = Layers::MOVING;
        }

        JPH::Ref<JPH::Shape> shape;
        float collider_friction    = 0.5f;
        float collider_restitution = 0.0f;
        if (entity.has_component<BoxCollider3DComponent>()) {
            auto& bc = entity.get_component<BoxCollider3DComponent>();
            JPH::BoxShapeSettings settings(JPH::Vec3(
                bc.half_size.x * tc.scale.x,
                bc.half_size.y * tc.scale.y,
                bc.half_size.z * tc.scale.z));
            collider_friction    = bc.friction;
            collider_restitution = bc.restitution;
            shape = settings.Create().Get();
        } else if (entity.has_component<SphereCollider3DComponent>()) {
            auto& sc = entity.get_component<SphereCollider3DComponent>();
            JPH::SphereShapeSettings settings(sc.radius * glm::max(tc.scale.x,
                glm::max(tc.scale.y, tc.scale.z)));
            collider_friction    = sc.friction;
            collider_restitution = sc.restitution;
            shape = settings.Create().Get();
        } else if (entity.has_component<CapsuleCollider3DComponent>()) {
            auto& cc = entity.get_component<CapsuleCollider3DComponent>();
            JPH::CapsuleShapeSettings settings(cc.half_height, cc.radius);
            collider_friction    = cc.friction;
            collider_restitution = cc.restitution;
            shape = settings.Create().Get();
        } else {
            // No collider - can't create a body
            return JPH::BodyID();
        }

        // Fill the body creation settings
        JPH::BodyCreationSettings bcs(
            shape,
            JPH::RVec3(tc.translation.x, tc.translation.y, tc.translation.z),
            JPH::Quat::sEulerAngles(JPH::Vec3(tc.rotation.x, tc.rotation.y, tc.rotation.z)),
            motion,
            layer
        );
        bcs.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
        bcs.mMassPropertiesOverride.mMass = rb.mass;
        bcs.mLinearDamping  = rb.linear_damping;
        bcs.mAngularDamping = rb.angular_damping;
        bcs.mFriction       = collider_friction;
        bcs.mRestitution    = collider_restitution;
        bcs.mIsSensor       = rb.is_sensor;
        if (!rb.gravity_factor) bcs.mGravityFactor = 0.0f;

        // Create and add the body
        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        JPH::Body* body = bi.CreateBody(bcs);
        if (!body) return JPH::BodyID(); // pool exhausted

        // Store the entity UUID in the body's user data (same as b2Body_SetUserData)
        body->SetUserData((JPH::uint64)entity.get_uuid());

        // Activate it and store the ID back in the component
        bi.AddBody(body->GetID(), JPH::EActivation::Activate);
        rb.runtime_body_id = body->GetID().GetIndexAndSequenceNumber();

        return body->GetID();
    }

    void PhysicsEngine3D::destroy_body(JPH::BodyID id) {
        if (!m_system || id.IsInvalid()) return;
        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        bi.RemoveBody(id);
        bi.DestroyBody(id);
    }

    void PhysicsEngine3D::sync_transform_to_body(Entity entity) {
        auto& rb = entity.get_component<RigidbodyComponent>();
        auto& tc = entity.get_component<TransformComponent>();
        JPH::BodyID id{rb.runtime_body_id};
        JPH::BodyInterface& bi = m_system->GetBodyInterface();
        bi.SetPositionAndRotation(
            id,
            JPH::RVec3(tc.translation.x, tc.translation.y, tc.translation.z),
            JPH::Quat::sEulerAngles(JPH::Vec3(tc.rotation.x, tc.rotation.y, tc.rotation.z)),
            JPH::EActivation::Activate
        );
    }

    void PhysicsEngine3D::sync_body_to_transform(Entity entity) {
        auto& rb = entity.get_component<RigidbodyComponent>();
        auto& tc = entity.get_component<TransformComponent>();
        JPH::BodyID id{rb.runtime_body_id};
        JPH::BodyInterface& bi = m_system->GetBodyInterface();

        JPH::RVec3 pos = bi.GetPosition(id);
        JPH::Quat  rot = bi.GetRotation(id);

        tc.translation = {pos.GetX(), pos.GetY(), pos.GetZ()};

        // Convert quaternion -> euler (glm handles this)
        glm::quat q(rot.GetW(), rot.GetX(), rot.GetY(), rot.GetZ());
        tc.rotation = glm::eulerAngles(q);
        tc.dirty = true;
    }

    void PhysicsEngine3D::apply_force(JPH::BodyID id, glm::vec3 f) {
        m_system->GetBodyInterface().AddForce(id, JPH::Vec3(f.x, f.y, f.z));
    }
    void PhysicsEngine3D::apply_impulse(JPH::BodyID id, glm::vec3 imp) {
        m_system->GetBodyInterface().AddImpulse(id, JPH::Vec3(imp.x, imp.y, imp.z));
    }
    void PhysicsEngine3D::set_velocity(JPH::BodyID id, glm::vec3 v) {
        m_system->GetBodyInterface().SetLinearVelocity(id, JPH::Vec3(v.x, v.y, v.z));
    }
    glm::vec3 PhysicsEngine3D::get_velocity(JPH::BodyID id) const {
        auto v = m_system->GetBodyInterface().GetLinearVelocity(id);
        return {v.GetX(), v.GetY(), v.GetZ()};
    }

    uint32_t PhysicsEngine3D::get_body_count() const {
        if (!m_system) return 0;
        return m_system->GetNumBodies();
    }

    uint32_t PhysicsEngine3D::get_active_body_count() const {
        if (!m_system) return 0;
        return m_system->GetNumActiveBodies(JPH::EBodyType::RigidBody);
    }

    PhysicsEngine3D& PhysicsEngine3D::get() {
        static PhysicsEngine3D instance;
        return instance;
    }

}
