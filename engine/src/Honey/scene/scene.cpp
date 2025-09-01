#include "hnpch.h"
#include "scene.h"
#include "components.h"
#include "scriptable_entity.h"
#include <glm/glm.hpp>

#include "entity.h"
#include "Honey/renderer/renderer.h"
#include "Honey/renderer/renderer_2d.h"

namespace Honey {

    Scene::Scene() {
        m_primary_camera_entity = new Entity();
    }

    Scene::~Scene() {
        delete m_primary_camera_entity;
    }

    Entity Scene::create_entity(const std::string &name) {
        Entity entity = Entity(m_registry.create(), this);

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

        m_registry.view<NativeScriptComponent>().each([this, ts](auto entity, auto& nsc) {

            // TODO: move to on_scene_play
            if (!nsc.instance) {
                nsc.instance = nsc.instantiate_script();
                nsc.instance->m_entity = Entity(entity, this);
                nsc.instance->on_create();
            }

            nsc.instance->on_update(ts);
        });

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
                    Renderer2D::draw_quad(entity_transform.get_transform(), sprite.color, (int)entity);
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
            Renderer2D::draw_quad(entity_transform.get_transform(), sprite.color, (int)entity);
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
