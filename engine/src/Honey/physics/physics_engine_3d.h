#pragma once
#include <Jolt/Jolt.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Core/TempAllocator.h>

#include "Honey/scene/entity.h"

namespace Honey {
    class Scene;
    class JoltJobSystem;
    class JoltContactListener;

    namespace Layers {
        static constexpr JPH::ObjectLayer NON_MOVING = 0;
        static constexpr JPH::ObjectLayer MOVING     = 1;
        static constexpr JPH::uint        NUM_LAYERS = 2;
    }
    namespace BPLayers {
        static constexpr JPH::BroadPhaseLayer NON_MOVING{0};
        static constexpr JPH::BroadPhaseLayer MOVING{1};
        static constexpr JPH::uint            NUM_LAYERS = 2;
    }

    class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
    public:
        JPH::uint GetNumBroadPhaseLayers() const override { return BPLayers::NUM_LAYERS; }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
            return (layer == Layers::MOVING) ? BPLayers::MOVING : BPLayers::NON_MOVING;
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
            switch ((JPH::BroadPhaseLayer::Type)layer) {
            case (JPH::BroadPhaseLayer::Type)BPLayers::NON_MOVING: return "NON_MOVING";
            case (JPH::BroadPhaseLayer::Type)BPLayers::MOVING:     return "MOVING";
            default: return "INVALID";
            }
        }
#endif
    };
    class OVBPFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override
        {
            if (layer == Layers::NON_MOVING)
                return bp == BPLayers::MOVING; // static only hits moving

            return true; // moving hits everything
        }
    };
    class OVOFilter final : public JPH::ObjectLayerPairFilter {
    public:
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
            if (a == Layers::NON_MOVING)
                return b == Layers::MOVING;

            return true;
        }
    };

    class PhysicsEngine3D {
    public:

        PhysicsEngine3D() = default;

        static void init();    // called once at engine startup (registers Jolt allocators + types)
        static void shutdown();

        // Per-scene lifetime
        void on_scene_start(Scene* scene);
        void on_scene_stop();

        // Called every game tick
        void step(float dt);

        // Body management
        JPH::BodyID create_body(Entity entity);
        void        destroy_body(JPH::BodyID id);
        void        sync_transform_to_body(Entity entity);    // editor drag → physics
        void        sync_body_to_transform(Entity entity);    // physics → ECS

        // Impulse / force API (called from CSharpScriptGlue / ScriptGlue)
        void apply_force   (JPH::BodyID id, glm::vec3 force);
        void apply_impulse (JPH::BodyID id, glm::vec3 impulse);
        void set_velocity  (JPH::BodyID id, glm::vec3 v);
        glm::vec3 get_velocity(JPH::BodyID id) const;

        uint32_t get_body_count() const;
        uint32_t get_active_body_count() const;

        static PhysicsEngine3D& get();

    private:
        static constexpr uint32_t k_max_bodies      = 65536;
        static constexpr uint32_t k_num_body_mutexes = 0;    // 0 = auto
        static constexpr uint32_t k_max_body_pairs  = 65536;
        static constexpr uint32_t k_max_contact_constraints = 16384;

        std::unique_ptr<JPH::PhysicsSystem>         m_system;
        std::unique_ptr<JoltJobSystem>              m_job_system;
        std::unique_ptr<JoltContactListener>        m_contact_listener;
        std::unique_ptr<JPH::TempAllocatorImpl>     m_temp_allocator;

        // Jolt requires these two allocation objects to outlive the system
        BPLayerInterfaceImpl    m_bp_layer_interface;
        OVBPFilter              m_obj_vs_bp_filter;
        OVOFilter               m_obj_vs_obj_filter;

        Scene* m_scene = nullptr;
    };
}
