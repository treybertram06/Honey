#include "hnpch.h"
#include "scene.h"
#include "components.h"
#include <glm/glm.hpp>

#include "Honey/renderer/renderer_2d.h"

namespace Honey {

    Scene::Scene() {


    }

    Scene::~Scene() {
    }

    entt::entity Scene::create_entity(const std::string &name) {
        return m_registry.create();
    }

    void Scene::on_update(Timestep ts) {
        auto group = m_registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
        for (auto entity : group) {
            auto [transform, sprite] = group.get<TransformComponent, SpriteRendererComponent>(entity);
            Renderer2D::draw_quad(transform, sprite.color);
        }
    }


}
