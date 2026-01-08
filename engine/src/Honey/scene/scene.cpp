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
#include "Honey/core/settings.h"
#include "Honey/math/math.h"
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

    Entity Scene::create_child_for(Entity parent, const std::string& name) {
        Entity child = create_entity(name);
        child.set_parent(parent);
        return child;
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
        clear_state();
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
        clear_state();
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

    static void rebuild_colliders(Scene* scene, Entity entity) {
        if (!entity.has_component<Rigidbody2DComponent>())
            return;

        auto& rb = entity.get_component<Rigidbody2DComponent>();

        b2BodyId body;
        memcpy(&body, &rb.runtime_body, sizeof(b2BodyId));
        if (!b2Body_IsValid(body))
            return;

        auto& tc = entity.get_component<TransformComponent>();

        // ---- BoxCollider ----
        if (entity.has_component<BoxCollider2DComponent>()) {
            auto& collider = entity.get_component<BoxCollider2DComponent>();

            for (b2ShapeId shape : collider.runtime_shapes) {
                if (b2Shape_IsValid(shape))
                    b2DestroyShape(shape, true);
            }
            collider.runtime_shapes.clear();

            b2ShapeDef shape_def = b2DefaultShapeDef();
            shape_def.density = collider.density;
            shape_def.enableContactEvents = true;

            b2SurfaceMaterial material{
                collider.friction,
                collider.restitution
            };

            b2Polygon box = b2MakeOffsetBox(
                tc.scale.x * collider.size.x,
                tc.scale.y * collider.size.y,
                { collider.offset.x, collider.offset.y },
                b2MakeRot(tc.rotation.z)
            );

            b2ShapeId shape = b2CreatePolygonShape(body, &shape_def, &box);
            b2Shape_SetSurfaceMaterial(shape, &material);

            collider.runtime_shapes.push_back(shape);
        }

        // ---- CircleCollider ----
        if (entity.has_component<CircleCollider2DComponent>()) {
            auto& collider = entity.get_component<CircleCollider2DComponent>();

            for (b2ShapeId shape : collider.runtime_shapes) {
                if (b2Shape_IsValid(shape))
                    b2DestroyShape(shape, true);
            }
            collider.runtime_shapes.clear();

            b2ShapeDef shape_def = b2DefaultShapeDef();
            shape_def.density = collider.density;
            shape_def.enableContactEvents = true;

            b2SurfaceMaterial material{
                collider.friction,
                collider.restitution
            };

            b2Circle circle{};
            circle.center = { collider.offset.x, collider.offset.y };
            circle.radius = tc.scale.x * collider.radius;

            b2ShapeId shape = b2CreateCircleShape(body, &shape_def, &circle);
            b2Shape_SetSurfaceMaterial(shape, &material);

            collider.runtime_shapes.push_back(shape);
        }
    }

    void Scene::on_update_runtime(Timestep ts) {
        s_active_scene = this;

        // Scripts
        {
            // Lua scripts
            //auto view = m_registry.view<ScriptComponent>();
            //for (auto e : view) {
            //    Entity entity = { e, this };
            //    auto& sc = entity.get_component<ScriptComponent>();
            //    if (!sc.initialized) {
            //        ScriptEngine::on_create_entity(entity);
            //        sc.initialized = true;
            //    }
            //    ScriptEngine::on_update_entity(entity, ts);
            //}
            auto view = m_registry.view<ScriptComponent>();
            std::vector<entt::entity> script_entities;
            script_entities.reserve(view.size());

            for (auto e : view)
                script_entities.push_back(e);

            for (auto e : script_entities) {
                if (!m_registry.valid(e))
                    continue;

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
        auto& physics_settings = get_settings().physics;
        if (physics_settings.enabled) {
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

                if (tc.collider_dirty) {
                    rebuild_colliders(this, entity);
                    tc.collider_dirty = false;
                }
            }

            const int32_t sub_steps = physics_settings.substeps;

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
                auto& tc = entity.get_component<TransformComponent>();
                auto& rb = entity.get_component<Rigidbody2DComponent>();

                b2BodyId body;
                memcpy(&body, &rb.runtime_body, sizeof(b2BodyId));

                if (!b2Body_IsValid(body))
                    continue;

                b2Transform bt = b2Body_GetTransform(body);
                glm::vec3 world_pos(bt.p.x, bt.p.y, tc.translation.z);
                float world_rot = b2Rot_GetAngle(bt.q);

                if (!entity.has_parent()) {
                    // Root Rigidbody: world == local
                    tc.translation = world_pos;
                    tc.rotation.z = world_rot;
                } else {
                    // Child Rigidbody: convert world → local
                    Entity parent = entity.get_parent();
                    glm::mat4 parent_world = parent.get_world_transform();
                    glm::mat4 inv_parent = glm::inverse(parent_world);

                    glm::mat4 world =
                        glm::translate(glm::mat4(1.0f), world_pos) *
                        glm::rotate(glm::mat4(1.0f), world_rot, {0,0,1});

                    glm::mat4 local = inv_parent * world;

                    Math::decompose_transform(
                        local,
                        tc.translation,
                        tc.rotation,
                        tc.scale
                    );
                }
            }
        }

        // render
        Entity primary_camera_entity = get_primary_camera();
        if (primary_camera_entity.is_valid()) {
            auto transform = primary_camera_entity.get_world_transform();
            auto& camera_component = primary_camera_entity.get_component<CameraComponent>();

            Camera* primary_camera = camera_component.get_camera();

            if (primary_camera) {
                Renderer2D::begin_scene(*primary_camera, transform);

                auto group = m_registry.group<SpriteRendererComponent>(entt::get<TransformComponent>);
                for (auto entity : group) {
                    auto sprite = group.get<SpriteRendererComponent>(entity);
                    Entity entity_ref = { entity, this };
                    Renderer2D::draw_sprite(entity_ref.get_world_transform(), sprite, (int)entity);
                }

                auto cicle_group = m_registry.group<CircleRendererComponent>(entt::get<TransformComponent>);
                for (auto entity : cicle_group) {
                    auto sprite = cicle_group.get<CircleRendererComponent>(entity);
                    Entity entity_ref = { entity, this };
                    Renderer2D::draw_circle_sprite(entity_ref.get_world_transform(), sprite, (int)entity);
                }

                auto line_group = m_registry.group<LineRendererComponent>(entt::get<TransformComponent>);
                for (auto entity : line_group) {
                    auto sprite = line_group.get<LineRendererComponent>(entity);
                    Entity entity_ref = { entity, this };
                    Renderer2D::draw_line_sprite(entity_ref.get_world_transform(), sprite, (int)entity);
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
            auto sprite = group.get<SpriteRendererComponent>(entity);
            Entity entity_ref = { entity, this };
            Renderer2D::draw_sprite(entity_ref.get_world_transform(), sprite, (int)entity);
        }

        auto cicle_group = m_registry.group<CircleRendererComponent>(entt::get<TransformComponent>);
        for (auto entity : cicle_group) {
            auto sprite = cicle_group.get<CircleRendererComponent>(entity);
            Entity entity_ref = { entity, this };
            Renderer2D::draw_circle_sprite(entity_ref.get_world_transform(), sprite, (int)entity);
        }

        auto line_group = m_registry.group<LineRendererComponent>(entt::get<TransformComponent>);
        for (auto entity : line_group) {
            auto sprite = line_group.get<LineRendererComponent>(entity);
            Entity entity_ref = { entity, this };
            Renderer2D::draw_line_sprite(entity_ref.get_world_transform(), sprite, (int)entity);
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
        copy_component<Rigidbody2DComponent>        (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<BoxCollider2DComponent>      (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<CircleCollider2DComponent>      (dst_scene_registry, src_scene_registry, entt_map);

        auto view = src_scene_registry.view<RelationshipComponent>();
        for (auto e : view) {
            const auto& srcRel = src_scene_registry.get<RelationshipComponent>(e);

            if (srcRel.parent == entt::null)
                continue;

            UUID childUUID  = src_scene_registry.get<IDComponent>(e).id;
            UUID parentUUID = src_scene_registry.get<IDComponent>(srcRel.parent).id;

            Entity child  = { entt_map.at(childUUID), copy.get() };
            Entity parent = { entt_map.at(parentUUID), copy.get() };

            child.set_parent(parent, false);
        }

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

    Entity Scene::add_prefab_to_scene(const std::string& path) {
        Ref<Scene> scene_ref = Ref<Scene>(this, [](Scene*){});
        SceneSerializer serializer(scene_ref);

        auto entity = serializer.deserialize_entity_prefab(path);
        //ScriptEngine::on_create_entity(entity); // Now handled by on_update
        return entity;
    }

    static void collect_collider_entities(Scene* scene, Entity root, std::vector<Entity>& out_entities) {
        out_entities.push_back(root);

        if (!root.has_component<RelationshipComponent>())
            return;

        auto& rel = root.get_component<RelationshipComponent>();
        for (auto child_handle : rel.children) {
            Entity child{ child_handle, scene };
            if (!child.is_valid())
                continue;

            // Stop at nested Rigidbody (Unity rule)
            if (child.has_component<Rigidbody2DComponent>())
                continue;

            collect_collider_entities(scene, child, out_entities);
        }
    }

    void Scene::create_physics_body(Entity entity) {

        if (entity.has_parent()) {
            HN_CORE_WARN("Rigidbody2D entity '{0}' has a parent. Detaching.", entity.get_tag());
            entity.set_parent(Entity{}, false);
        }

        //auto& root_tc = entity.get_component<TransformComponent>();
        auto& rb = entity.get_component<Rigidbody2DComponent>();

        glm::mat4 world = entity.get_world_transform();

        glm::vec3 world_translation;
        glm::vec3 world_rotation;
        glm::vec3 world_scale;
        Math::decompose_transform(world, world_translation, world_rotation, world_scale);

        b2BodyDef body_def = b2DefaultBodyDef();
        body_def.type = hn_rigidbody2d_type_to_box2d_type(rb.body_type);
        body_def.position = { world_translation.x, world_translation.y };
        body_def.rotation = b2MakeRot(world_rotation.z);

        b2BodyId body = b2CreateBody(m_world, &body_def);

        b2Body_SetUserData(body, (void*)(uint64_t)entity.get_uuid());
        b2Body_SetMotionLocks(body, { false, false, rb.fixed_rotation });

        memcpy(&rb.runtime_body, &body, sizeof(b2BodyId));

        // ---- COMPOUND COLLIDERS ----
        std::vector<Entity> collider_entities;
        collect_collider_entities(this, entity, collider_entities);

        glm::mat4 root_world = entity.get_world_transform();
        glm::mat4 inv_root = glm::inverse(root_world);

        for (Entity e : collider_entities) {
            auto& tc = e.get_component<TransformComponent>();
            glm::mat4 local_to_root = inv_root * e.get_world_transform();

            glm::vec3 rel_pos;
            glm::vec3 rel_rot;
            glm::vec3 rel_scale;
            Math::decompose_transform(local_to_root, rel_pos, rel_rot, rel_scale);

            // ---- BoxCollider ----
            if (e.has_component<BoxCollider2DComponent>()) {
                auto& bc = e.get_component<BoxCollider2DComponent>();

                b2ShapeDef shape_def = b2DefaultShapeDef();
                shape_def.density = bc.density;
                shape_def.enableContactEvents = true;

                b2SurfaceMaterial mat{ bc.friction, bc.restitution };

                b2Polygon box = b2MakeOffsetBox(
                    rel_scale.x * bc.size.x,
                    rel_scale.y * bc.size.y,
                    { rel_pos.x + bc.offset.x, rel_pos.y + bc.offset.y },
                    b2MakeRot(rel_rot.z)
                );

                b2ShapeId shape = b2CreatePolygonShape(body, &shape_def, &box);
                b2Shape_SetSurfaceMaterial(shape, &mat);

                bc.runtime_shapes.push_back(shape);
            }

            // ---- CircleCollider ----
            if (e.has_component<CircleCollider2DComponent>()) {
                auto& cc = e.get_component<CircleCollider2DComponent>();

                b2ShapeDef shape_def = b2DefaultShapeDef();
                shape_def.density = cc.density;
                shape_def.enableContactEvents = true;

                b2SurfaceMaterial mat{ cc.friction, cc.restitution };

                b2Circle circle{};
                circle.center = {
                    rel_pos.x + cc.offset.x,
                    rel_pos.y + cc.offset.y
                };
                circle.radius = rel_scale.x * cc.radius;

                b2ShapeId shape = b2CreateCircleShape(body, &shape_def, &circle);
                b2Shape_SetSurfaceMaterial(shape, &mat);

                cc.runtime_shapes.push_back(shape);
            }
        }
    }
}
