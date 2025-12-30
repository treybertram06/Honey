#include "hnpch.h"
#include "scene.h"
#include "components.h"
#include "scriptable_entity.h"
#include "Honey/renderer/renderer_2d.h"
#include "Honey/renderer/renderer.h"
#include "entity.h"

#include <glm/glm.hpp>
#include <box2d/box2d.h>

#include "scene_serializer.h"
#include "Honey/scripting/script_engine.h"
//#include "Honey/scripting/mono_script_engine.h"

namespace Honey {

    static b2BodyType hn_rigidbody2d_type_to_box2d_type(Rigidbody2DComponent::BodyType type) {
        switch (type) {
            case Rigidbody2DComponent::BodyType::Static: return b2_staticBody;
            case Rigidbody2DComponent::BodyType::Dynamic: return b2_dynamicBody;
            case Rigidbody2DComponent::BodyType::Kinematic: return b2_kinematicBody;
        }

        HN_CORE_ASSERT(false, "Unknown Rigidbody2D type!");
        return b2_staticBody;
    }

    Scene* Scene::s_active_scene = nullptr;

    Scene::Scene() {
    }

    Scene::~Scene() {
        if (b2World_IsValid(m_world))
            b2DestroyWorld(m_world);
    }

    Entity Scene::create_entity(const std::string &name) {
        return create_entity(name, UUID());
    }

    Entity Scene::create_entity(const std::string &name, UUID uuid) {
        Entity entity = Entity(m_registry.create(), this);

        entity.add_component<IDComponent>(uuid);
        entity.add_component<TransformComponent>();
        entity.add_component<TagComponent>(name.empty() ? "Entity" : name);

        return entity;
    }


    void Scene::destroy_entity(Entity entity) {
        if (!entity.is_valid())
            return;

        if (entity.has_component<RelationshipComponent>()) {
            auto& relationship = entity.get_component<RelationshipComponent>();

            // Copy the list because destroy_entity mutates the registry
            auto children = relationship.children;
            for (auto child : children) {
                if (m_registry.valid(child)) {
                    destroy_entity(Entity{ child, this });
                }
            }
        }

        if (entity.has_component<RelationshipComponent>()) {
            auto& relationship = entity.get_component<RelationshipComponent>();
            if (relationship.parent != entt::null && m_registry.valid(relationship.parent)) {
                auto& parent_rel = m_registry.get<RelationshipComponent>(relationship.parent);
                auto& siblings = parent_rel.children;

                siblings.erase(
                    std::remove(siblings.begin(), siblings.end(), (entt::entity)entity),
                    siblings.end()
                );
            }
        }

        ScriptEngine::on_destroy_entity(entity);
        m_registry.destroy(entity);
    }

    Entity Scene::get_entity(UUID uuid) {
        auto view = m_registry.view<IDComponent>();
        for (auto e : view) {
            auto& id = view.get<IDComponent>(e);
            if (id.id == uuid)
                return Entity{ e, this };
        }

        return {};
    }

    void Scene::on_physics_2D_start() {
        b2WorldDef world_def = b2DefaultWorldDef();
        world_def.gravity = {0.0f, -9.81f};
        m_world = b2CreateWorld(&world_def);
        b2World_SetRestitutionThreshold(m_world, 0.5f);

        auto view = m_registry.view<Rigidbody2DComponent>();
        for (auto e : view) {
            Entity entity = { e, this };
            create_physics_body(entity);
        }
    }

    void Scene::on_physics_2D_stop() {
        if (b2World_IsValid(m_world)) {
            b2DestroyWorld(m_world);
            m_world = b2_nullWorldId;
        }
    }

    void Scene::on_runtime_start() {
        on_physics_2D_start();

        // Scripting
        {
            ScriptEngine::on_runtime_start(this);
            // Instantiate all script entities

            //auto view = m_registry.view<ScriptComponent>();
            //for (auto e : view) {
            //    Entity entity = { e, this };
            //    ScriptEngine::on_create_entity(entity);
            //
            //} // Every entities on_create is called automatically if its 'initialized' flag is false
        }
    }

    void Scene::on_runtime_stop() {
        on_physics_2D_stop();
        ScriptEngine::on_runtime_stop();
    }

    Entity Scene::get_primary_camera() const {
        auto view = m_registry.view<CameraComponent>();
        for (auto entity : view) {
            auto& camera = view.get<CameraComponent>(entity);
            if (camera.primary)
                return Entity(entity, const_cast<Scene*>(this));
        }
        return {}; // invalid entity if none found
    }

    void Scene::on_update_runtime(Timestep ts) {
        s_active_scene = this;

        // Scripts
        {
            // Lua scripts
            auto view = m_registry.view<ScriptComponent>();
            for (auto e : view) {
                Entity entity = { e, this };
                auto& sc = entity.get_component<ScriptComponent>();
                if (!sc.initialized) {
                    ScriptEngine::on_create_entity(entity);
                    sc.initialized = true;
                }
                ScriptEngine::on_update_entity(entity, ts);
            }

            // C++ scripts
            m_registry.view<NativeScriptComponent>().each([this, ts](auto entity, auto& nsc) {
                if (!nsc.instance) {
                    nsc.instance = nsc.instantiate_script();
                    nsc.instance->m_entity = Entity(entity, this);
                    nsc.instance->on_create();
                }
                nsc.instance->on_update(ts);
            });
        }
        //physics
        {
            // Sync physics bodies with their transforms if needed
            for (auto e : m_registry.view<TransformComponent, Rigidbody2DComponent>()) {
                Entity entity = { e, this };
                auto& tc = entity.get_component<TransformComponent>();
                if (tc.dirty) {
                    auto& rb = entity.get_component<Rigidbody2DComponent>();

                    b2BodyId body;
                    memcpy(&body, &rb.runtime_body, sizeof(b2BodyId));

                    b2Body_SetTransform(body, { tc.translation.x, tc.translation.y }, b2MakeRot(tc.rotation.z));
                    tc.dirty = false;
                }
            }

            const int32_t sub_steps = 6;

            if (b2World_IsValid(m_world)) {
                b2World_Step(m_world, ts, sub_steps);
            }

            b2ContactEvents events = b2World_GetContactEvents(m_world);

            for (int i = 0; i < events.beginCount; i++) {
                b2ContactBeginTouchEvent* evt = &events.beginEvents[i];

                b2BodyId body_a = b2Shape_GetBody(evt->shapeIdA);
                b2BodyId body_b = b2Shape_GetBody(evt->shapeIdB);

                UUID uuid_a = (uint64_t)b2Body_GetUserData(body_a);
                UUID uuid_b = (uint64_t)b2Body_GetUserData(body_b);

                Entity entity_a = get_entity(uuid_a);
                Entity entity_b = get_entity(uuid_b);

                if (entity_a.is_valid() && entity_b.is_valid()) {
                    ScriptEngine::on_collision_begin(entity_a, entity_b); // I could be checking if entity_a has a script component before calling this
                    ScriptEngine::on_collision_begin(entity_b, entity_a); // Likewise but entity_b
                }
            }
            for (int i = 0; i < events.endCount; i++) {
                b2ContactEndTouchEvent* evt = &events.endEvents[i];

                b2BodyId body_a = b2Shape_GetBody(evt->shapeIdA);
                b2BodyId body_b = b2Shape_GetBody(evt->shapeIdB);

                UUID uuid_a = (uint64_t)b2Body_GetUserData(body_a);
                UUID uuid_b = (uint64_t)b2Body_GetUserData(body_b);

                Entity entity_a = get_entity(uuid_a);
                Entity entity_b = get_entity(uuid_b);

                if (entity_a.is_valid() && entity_b.is_valid()) {
                    ScriptEngine::on_collision_end(entity_a, entity_b);
                    ScriptEngine::on_collision_end(entity_b, entity_a);
                }
            }

            auto view = m_registry.view<Rigidbody2DComponent>();
            for (auto e : view) {
                Entity entity = { e, this };
                auto& transform = entity.get_component<TransformComponent>();
                auto& rigidbody2d = entity.get_component<Rigidbody2DComponent>();

                b2BodyId body;
                memcpy(&body, &rigidbody2d.runtime_body, sizeof(b2BodyId));

                if (b2Body_IsValid(body)) {
                    b2Transform bodyTransform = b2Body_GetTransform(body);
                    b2Vec2 position = bodyTransform.p;
                    float angle = b2Rot_GetAngle(bodyTransform.q);

                    transform.translation.x = position.x;
                    transform.translation.y = position.y;
                    transform.rotation.z = angle;
                }
            }
        }

        // render
        Entity primary_camera_entity = get_primary_camera();
        if (primary_camera_entity.is_valid()) {
            auto transform = primary_camera_entity.get_component<TransformComponent>().get_transform();
            auto& camera_component = primary_camera_entity.get_component<CameraComponent>();

            Camera* primary_camera = camera_component.get_camera();

            if (primary_camera) {
                Renderer2D::begin_scene(*primary_camera, transform);

                auto group = m_registry.group<SpriteRendererComponent>(entt::get<TransformComponent>);
                for (auto entity : group) {
                    auto [sprite, entity_transform] = group.get<SpriteRendererComponent, TransformComponent>(entity);
                    Renderer2D::draw_sprite(entity_transform.get_transform(), sprite, (int)entity);
                }

                auto cicle_group = m_registry.group<CircleRendererComponent>(entt::get<TransformComponent>);
                for (auto entity : cicle_group) {
                    auto [sprite, entity_transform] = cicle_group.get<CircleRendererComponent, TransformComponent>(entity);
                    Renderer2D::draw_circle_sprite(entity_transform.get_transform(), sprite, (int)entity);
                }

                auto line_group = m_registry.group<LineRendererComponent>(entt::get<TransformComponent>);
                for (auto entity : line_group) {
                    auto [sprite, entity_transform] = line_group.get<LineRendererComponent, TransformComponent>(entity);
                    Renderer2D::draw_line_sprite(entity_transform.get_transform(), sprite, (int)entity);
                }

                Renderer2D::end_scene();
            }
        }

    }

    void Scene::on_update_editor(Timestep ts, EditorCamera& camera) {
        // render
        Renderer2D::begin_scene(camera);

        auto group = m_registry.group<SpriteRendererComponent>(entt::get<TransformComponent>);
        for (auto entity : group) {
            auto [sprite, entity_transform] = group.get<SpriteRendererComponent, TransformComponent>(entity);
            Renderer2D::draw_sprite(entity_transform.get_transform(), sprite, (int)entity);
        }

        auto cicle_group = m_registry.group<CircleRendererComponent>(entt::get<TransformComponent>);
        for (auto entity : cicle_group) {
            auto [sprite, entity_transform] = cicle_group.get<CircleRendererComponent, TransformComponent>(entity);
            Renderer2D::draw_circle_sprite(entity_transform.get_transform(), sprite, (int)entity);
        }

        auto line_group = m_registry.group<LineRendererComponent>(entt::get<TransformComponent>);
        for (auto entity : line_group) {
            auto [sprite, entity_transform] = line_group.get<LineRendererComponent, TransformComponent>(entity);
            Renderer2D::draw_line_sprite(entity_transform.get_transform(), sprite, (int)entity);
        }

        Renderer2D::end_scene();
    }

    void Scene::on_viewport_resize(uint32_t width, uint32_t height) {
        Renderer::on_window_resize(width, height);

        // Update camera aspect ratios for all camera entities
        float aspect_ratio = (float)width / (float)height;

        auto view = m_registry.view<CameraComponent>();
        for (auto entity : view) {
            auto& camera_component = view.get<CameraComponent>(entity);
            if (!camera_component.fixed_aspect_ratio) {
                camera_component.update_projection(aspect_ratio);
            }
        }
    }

    template<typename Component>
    static void copy_component(entt::registry& dst, const entt::registry& src, std::unordered_map<UUID, entt::entity> entt_map) {
        auto view = src.view<Component>();
        for (auto e : view) {
            UUID uuid = src.get<IDComponent>(e).id;
            entt::entity dst_entt_id = entt_map.at(uuid);

            auto& component = src.get<Component>(e);
            dst.emplace_or_replace<Component>(dst_entt_id, component);
        }
    }

    template<typename Component>
    static void copy_component_if_exists(Entity dst, Entity src) {
        if (src.has_component<Component>()) {
            dst.add_or_replace_component<Component>(src.get_component<Component>());
        }
    }

    Ref<Scene> Scene::copy(Ref<Scene> source) {
        Ref<Scene> copy = CreateRef<Scene>();

        auto& src_scene_registry = source->get_registry();
        auto& dst_scene_registry = copy->get_registry();
        std::unordered_map<UUID, entt::entity> entt_map;

        auto id_view = src_scene_registry.view<IDComponent>();
        for (auto e : id_view) {
            const auto& tag = src_scene_registry.get<TagComponent>(e).tag;
            UUID uuid = src_scene_registry.get<IDComponent>(e).id;
            Entity new_entity = copy->create_entity(tag, uuid);
            entt_map[uuid] = (entt::entity)new_entity;
        }

        copy_component<TransformComponent>          (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<SpriteRendererComponent>     (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<CircleRendererComponent>     (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<LineRendererComponent>     (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<CameraComponent>             (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<NativeScriptComponent>       (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<ScriptComponent>             (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<RelationshipComponent>       (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<Rigidbody2DComponent>        (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<BoxCollider2DComponent>      (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<CircleCollider2DComponent>      (dst_scene_registry, src_scene_registry, entt_map);

        auto src_view = src_scene_registry.view<CameraComponent>();
        for (auto e : src_view) {
            auto& src_camera = src_view.get<CameraComponent>(e);
            if (src_camera.primary) {
                UUID uuid = src_scene_registry.get<IDComponent>(e).id;
                if (entt_map.contains(uuid)) {
                    Entity new_primary = { entt_map[uuid], copy.get() };
                    auto& dst_camera = new_primary.get_component<CameraComponent>();
                    dst_camera.primary = true;
                }
                break;
            }
        }

        return copy;
    }

    void Scene::duplicate_entity(Entity entity) {
        std::string new_name = entity.get_tag() + " (copy)";
        Entity new_entity = create_entity(new_name);

        copy_component_if_exists<TransformComponent>        (new_entity, entity);
        copy_component_if_exists<SpriteRendererComponent>   (new_entity, entity);
        copy_component_if_exists<CircleRendererComponent>   (new_entity, entity);
        copy_component_if_exists<LineRendererComponent>   (new_entity, entity);
        copy_component_if_exists<CameraComponent>           (new_entity, entity);
        copy_component_if_exists<NativeScriptComponent>     (new_entity, entity);
        copy_component_if_exists<ScriptComponent>           (new_entity, entity);
        copy_component_if_exists<RelationshipComponent>     (new_entity, entity);
        copy_component_if_exists<Rigidbody2DComponent>      (new_entity, entity);
        copy_component_if_exists<BoxCollider2DComponent>    (new_entity, entity);
        copy_component_if_exists<CircleCollider2DComponent>    (new_entity, entity);

    }
    //void Scene::create_prefab(const Entity& entity, const std::string& path) {
    //
    //}
//
    Entity Scene::instantiate_prefab(const std::string& path) {
        Ref<Scene> scene_ref = Ref<Scene>(this, [](Scene*){});
        SceneSerializer serializer(scene_ref);

        auto entity = serializer.deserialize_entity_prefab(path);
        create_physics_body(entity);
        //ScriptEngine::on_create_entity(entity); // Now handled by on_update
        return entity;
    }

    void Scene::create_physics_body(Entity entity) {
        auto& transform = entity.get_component<TransformComponent>();
        auto& rigidbody2d = entity.get_component<Rigidbody2DComponent>();

        b2BodyDef body_def = b2DefaultBodyDef();
        body_def.type = hn_rigidbody2d_type_to_box2d_type(rigidbody2d.body_type);
        body_def.position = { transform.translation.x, transform.translation.y };
        body_def.rotation = b2MakeRot(transform.rotation.z);

        b2BodyId body = b2CreateBody(m_world, &body_def);

        UUID uuid = entity.get_uuid();
        b2Body_SetUserData(body, (void*)(uint64_t)uuid);

        auto locks = b2MotionLocks{ false, false, rigidbody2d.fixed_rotation }; // do lin x & y need to be set to true based on body type?
        b2Body_SetMotionLocks(body, locks);
        memcpy(&rigidbody2d.runtime_body, &body, sizeof(b2BodyId));
        // retrieval example so I dont forget
        //b2BodyId body;
        //memcpy(&body, &rigidbody2d.runtime_body, sizeof(b2BodyId));

        if (entity.has_component<BoxCollider2DComponent>()) {
            auto& collider = entity.get_component<BoxCollider2DComponent>();

            b2ShapeDef shape_def = b2DefaultShapeDef();
            shape_def.density = collider.density;

            b2SurfaceMaterial material;
            material.friction = collider.friction;
            material.restitution = collider.restitution;

            b2Polygon box = b2MakeOffsetBox(
                transform.scale.x * collider.size.x,
                transform.scale.y * collider.size.y,
                { collider.offset.x, collider.offset.y },
                b2MakeRot(transform.rotation.z)
            );
            b2ShapeId shape = b2CreatePolygonShape(body, &shape_def, &box);

            b2Shape_SetSurfaceMaterial(shape, &material);
        }

        if (entity.has_component<CircleCollider2DComponent>()) {
            auto& collider = entity.get_component<CircleCollider2DComponent>();

            b2ShapeDef shape_def = b2DefaultShapeDef();
            shape_def.density = collider.density;

            b2SurfaceMaterial material;
            material.friction = collider.friction;
            material.restitution = collider.restitution;

            b2Circle circle{};
            circle.center = { collider.offset.x, collider.offset.y };
            circle.radius = transform.scale.x * collider.radius;
            b2ShapeId shape = b2CreateCircleShape(body, &shape_def, &circle);

            b2Shape_SetSurfaceMaterial(shape, &material);
        }
    }
}
