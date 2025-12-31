#pragma once

#include <entt/entt.hpp>

#include "Honey/core/timestep.h"
#include "Honey/core/uuid.h"
#include "Honey/renderer/editor_camera.h"
#include <box2d/id.h>

namespace Honey {

    class Entity;
    class Scene {

    public:
        Scene();
        ~Scene();

        Entity create_entity(const std::string& name = "");
        Entity create_entity(const std::string& name, UUID uuid);
        void destroy_entity(Entity entity);
        Entity create_child_for(Entity parent, const std::string& name = "");

        Entity get_entity(UUID uuid);

        void on_physics_2D_start();
        void on_physics_2D_stop();
        void on_runtime_start();
        void on_runtime_stop();

        void on_update_runtime(Timestep ts);
        void on_update_editor(Timestep ts, EditorCamera& camera);

        Entity get_primary_camera() const;

        entt::registry& get_registry() { return m_registry; }
        const entt::registry& get_registry() const { return m_registry; }

        void on_viewport_resize(uint32_t width, uint32_t height);

        static Scene* get_active_scene() { return s_active_scene; }
        static void set_active_scene(Scene* scene) { s_active_scene = scene; }

        static Ref<Scene> copy(Ref<Scene> source);

        void duplicate_entity(Entity entity);

        //void create_prefab(const Entity& entity, const std::string& path);
        Entity instantiate_prefab(const std::string& path);

        void create_physics_body(Entity entity);

        template<typename... Components>
        auto get_all_entities_with() {
            return m_registry.view<Components...>();
        }


    private:
        static Scene* s_active_scene;
        entt::registry m_registry;

        b2WorldId m_world = b2_nullWorldId;

        //Entity entity_from_body(b2BodyId body);

        friend class Entity;
        friend class SceneSerializer;
        friend class SceneHierarchyPanel;
    };
}
