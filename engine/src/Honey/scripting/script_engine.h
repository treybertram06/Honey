#pragma once

#include "Honey/scene/entity.h"
#include "Honey/core/timestep.h"
#include <memory>

namespace sol { class state; }

namespace Honey {

    class Scene;

    /*
    * Lua Script Contract:
    *    - self: Entity (injected)
    *    - dt: number (injected in OnUpdate only)
    *
    *    Callbacks:
    *    - OnCreate()
    *    - OnUpdate()
    *    - OnDestroy()
    *    - OnCollisionBegin(other)
    *    - OnCollisionEnd(other)
    */

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

        template<typename T>
        static std::optional<T> get_property(Entity entity, const std::string& name);

        template<typename T>
        static T get_property_or(Entity entity, const std::string& name, const T& fallback) {
            auto value = get_property<T>(entity, name);
            return value.has_value() ? *value : fallback;
        }

        static std::unordered_map<std::string, std::variant<
            float,
            bool,
            std::string
        >> get_default_properties(Entity entity);

    private:
        struct ScriptEngineData;
        static std::unique_ptr<ScriptEngineData> s_data;
    };

}
