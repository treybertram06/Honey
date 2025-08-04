#include "hnpch.h"
#include "scene.h"
#include "components.h"
#include "scriptable_entity.h"
#include <glm/glm.hpp>

#include "entity.h"
#include "Honey/renderer/renderer_2d.h"

namespace Honey {

    Scene::Scene() {


    }

    Scene::~Scene() {
    }

    Entity Scene::create_entity(const std::string &name) {
        Entity entity = Entity(m_registry.create(), this);

        entity.add_component<TransformComponent>();
        entity.add_component<TagComponent>(name.empty() ? "Entity" : name);

        return entity;
    }

    void Scene::destroy_entity(Entity entity) {
        if (entity.is_valid()) {
            m_registry.destroy(entity);
        }
    }


    void Scene::on_update(Timestep ts) {

        m_registry.view<NativeScriptComponent>().each([=](auto entity, auto& nsc) {

            if (!nsc.instance) {
                nsc.instantiate_function();
                nsc.instance->m_entity = Entity(entity, this);
                nsc.on_create_function(nsc.instance);
            }

            nsc.on_update_function(nsc.instance, ts);
        });

    }

    void Scene::render() {
        Camera* primary_camera = nullptr;
        glm::mat4* primary_transform = nullptr;
        {
            auto view = m_registry.view<TransformComponent, CameraComponent>();
            for (auto entity : view) {
                auto [transform, camera_component] = view.get<TransformComponent, CameraComponent>(entity);

                if (camera_component.primary) {
                    primary_camera = camera_component.get_camera();
                    primary_transform = &transform.transform;
                    break;
                }
            }
        }

        if (primary_camera) {
            Renderer2D::begin_scene(*primary_camera, *primary_transform);

            auto group = m_registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            for (auto entity : group) {
                auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
                Renderer2D::draw_quad(transform, sprite.color);
            }

            Renderer2D::end_scene();
        }
    }
}
