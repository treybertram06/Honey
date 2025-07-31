#include "hnpch.h"
#include "scene.h"
#include "components.h"
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

    }

    void Scene::render() {
        auto group = m_registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
        for (auto entity : group) {
            auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
            Renderer2D::draw_quad(transform, sprite.color);
        }
    }



}
