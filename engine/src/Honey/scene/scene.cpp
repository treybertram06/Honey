#include "hnpch.h"
#include "scene.h"
#include "components.h"
#include "scriptable_entity.h"
#include <glm/glm.hpp>

#include "entity.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/renderer_2d.h"
//#include "Honey/scripting/mono_script_engine.h"

namespace Honey {

    Scene* Scene::s_active_scene = nullptr;

    Scene::Scene() {
        m_primary_camera_entity = new Entity();
    }

    Scene::~Scene() {
        delete m_primary_camera_entity;
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
            if (m_has_primary_camera && entity == *m_primary_camera_entity) {
                clear_primary_camera();
            }

            m_registry.destroy(entity);
        }
    }

    Entity Scene::get_primary_camera() const {
        return *m_primary_camera_entity;
    }


    void Scene::set_primary_camera(Entity camera_entity) {
        if (camera_entity.is_valid() && camera_entity.has_component<CameraComponent>()) {
            *m_primary_camera_entity = camera_entity;
            m_has_primary_camera = true;
        }
    }

    void Scene::clear_primary_camera() {
        m_has_primary_camera = false;
        *m_primary_camera_entity = Entity();
    }

    void Scene::on_update_runtime(Timestep ts) {
        s_active_scene = this;

        m_registry.view<NativeScriptComponent>().each([this, ts](auto entity, auto& nsc) {

            // TODO: move to on_scene_play
            if (!nsc.instance) {
                nsc.instance = nsc.instantiate_script();
                nsc.instance->m_entity = Entity(entity, this);
                nsc.instance->on_create();
            }

            nsc.instance->on_update(ts);
        });

        /*m_registry.view<ScriptComponent>().each([this, &ts](auto entity, ScriptComponent& script) {
            auto id = m_registry.get<IDComponent>(entity).id;

            // If this entity doesn't have an active script instance, create one
            if (!Scripting::MonoScriptEngine::get_active_scripts().contains(id))
            {
                MonoClass* klass = Scripting::MonoScriptEngine::find_class(script.class_name);
                if (!klass)
                {
                    HN_CORE_ERROR("[Mono] Could not find class: {0}", script.class_name);
                    return;
                }

                // Create the managed script instance
                MonoObject* instance = mono_object_new(Scripting::MonoScriptEngine::get_domain(), klass);
                mono_runtime_object_init(instance);

                // Create a managed Entity object and assign it to the script
                MonoClassField* entity_field = mono_class_get_field_from_name(klass, "Entity");
                if (entity_field)
                {
                    MonoClass* entity_class = mono_class_from_name(Scripting::MonoScriptEngine::get_image(), "Honey", "Entity");
                    MonoObject* managed_entity = mono_object_new(Scripting::MonoScriptEngine::get_domain(), entity_class);
                    mono_runtime_object_init(managed_entity);

                    // Set the ID field on the managed Entity
                    MonoClassField* id_field = mono_class_get_field_from_name(entity_class, "ID");
                    if (id_field)
                        mono_field_set_value(managed_entity, id_field, &id);

                    // Assign the managed Entity to the script instance
                    mono_field_set_value(instance, entity_field, managed_entity);
                }

                // Cache script methods
                auto& script_classes = Scripting::MonoScriptEngine::get_script_classes();
                auto& script_class = script_classes[script.class_name];
                script_class.klass = klass;
                script_class.on_create = mono_class_get_method_from_name(klass, "OnCreate", 0);
                script_class.on_destroy = mono_class_get_method_from_name(klass, "OnDestroy", 0);
                script_class.on_update = mono_class_get_method_from_name(klass, "OnUpdate", 1);

                // Create and store the script instance
                Scripting::ScriptInstance script_instance { &script_class, instance };
                auto& active_scripts = Scripting::MonoScriptEngine::get_active_scripts();
                active_scripts[id] = script_instance;

                // Call OnCreate
                script_instance.invoke_on_create();
            }

            // Update existing instance
            auto& active_scripts = Scripting::MonoScriptEngine::get_active_scripts();
            auto& instance = active_scripts[id];
            instance.invoke_on_update(ts.get_seconds());
        });*/


        // render
        if (m_has_primary_camera && m_primary_camera_entity->is_valid()) {
            auto transform = m_primary_camera_entity->get_component<TransformComponent>().get_transform();
            auto& camera_component = m_primary_camera_entity->get_component<CameraComponent>();

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

}
