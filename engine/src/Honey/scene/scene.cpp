#include "hnpch.h"
#include "scene.h"
#include "components.h"
#include "scriptable_entity.h"
#include "Honey/renderer/renderer_2d.h"
#include "Honey/renderer/renderer.h"
#include "entity.h"

#include <glm/glm.hpp>
#include <box2d/box2d.h>

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
        if (entity.is_valid()) {
            m_registry.destroy(entity);
        }
    }

    void Scene::on_physics_2D_start() {
        b2WorldDef world_def = b2DefaultWorldDef();
        world_def.gravity = {0.0f, -9.81f};
        m_world = b2CreateWorld(&world_def);
        b2World_SetRestitutionThreshold(m_world, 0.5f);

        auto view = m_registry.view<Rigidbody2DComponent>();
        for (auto e : view) {
            Entity entity = { e, this };
            auto& transform = entity.get_component<TransformComponent>();
            auto& rigidbody2d = entity.get_component<Rigidbody2DComponent>();

            b2BodyDef body_def = b2DefaultBodyDef();
            body_def.type = hn_rigidbody2d_type_to_box2d_type(rigidbody2d.body_type);
            body_def.position = { transform.translation.x, transform.translation.y };
            body_def.rotation = b2MakeRot(transform.rotation.z);

            b2BodyId body = b2CreateBody(m_world, &body_def);
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

                b2Polygon box = b2MakeBox(transform.scale.x * collider.size.x, transform.scale.y * collider.size.y);
                b2ShapeId shape = b2CreatePolygonShape(body, &shape_def, &box);

                b2Shape_SetSurfaceMaterial(shape, &material);

            }

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

            auto view = m_registry.view<ScriptComponent>();
            for (auto e : view) {
                Entity entity = { e, this };
                ScriptEngine::on_create_entity(entity);

            }
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
            // C# scripts
            auto view = m_registry.view<ScriptComponent>();
            for (auto e : view) {
                Entity entity = { e, this };
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
            const int32_t sub_steps = 6;

            if (b2World_IsValid(m_world)) {
                b2World_Step(m_world, ts, sub_steps);
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

                auto group = m_registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
                for (auto entity : group) {
                    auto [entity_transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
                    Renderer2D::draw_sprite(entity_transform.get_transform(), sprite, (int)entity);
                }

                Renderer2D::end_scene();
            }
        }

    }

    void Scene::on_update_editor(Timestep ts, EditorCamera& camera) {
        // render
        Renderer2D::begin_scene(camera);

        auto group = m_registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
        for (auto entity : group) {
            auto [entity_transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
            Renderer2D::draw_sprite(entity_transform.get_transform(), sprite, (int)entity);
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
        copy_component<CameraComponent>             (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<NativeScriptComponent>       (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<ScriptComponent>             (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<RelationshipComponent>       (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<Rigidbody2DComponent>        (dst_scene_registry, src_scene_registry, entt_map);
        copy_component<BoxCollider2DComponent>      (dst_scene_registry, src_scene_registry, entt_map);

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
        copy_component_if_exists<CameraComponent>           (new_entity, entity);
        copy_component_if_exists<NativeScriptComponent>     (new_entity, entity);
        copy_component_if_exists<ScriptComponent>           (new_entity, entity);
        copy_component_if_exists<RelationshipComponent>     (new_entity, entity);
        copy_component_if_exists<Rigidbody2DComponent>      (new_entity, entity);
        copy_component_if_exists<BoxCollider2DComponent>    (new_entity, entity);

    }

}
