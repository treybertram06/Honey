#pragma once

#include "Honey/scene/entity.h"
#include "Honey/core/timestep.h"
#include <memory>

namespace sol { class state; }

namespace Honey {

    class Scene;

    class ScriptEngine {
    public:
        static void init();
        static void shutdown();

        // Runtime
        static void on_runtime_start(Scene* scene);
        static void on_runtime_stop();

        // Entity lifecycle
        static bool entity_class_exists(const std::string& class_name);
        static void on_create_entity(Entity entity);
        static void on_update_entity(Entity entity, Timestep ts);
        static void on_destroy_entity(Entity entity);

        // Collision events
        static void on_collision_begin(Entity a, Entity b);
        static void on_collision_end(Entity a, Entity b);

        // Accessors
        static Scene* get_scene_context();
        static sol::state& get_lua_state();

    private:
        struct ScriptEngineData;
        static std::unique_ptr<ScriptEngineData> s_data;
    };

}
