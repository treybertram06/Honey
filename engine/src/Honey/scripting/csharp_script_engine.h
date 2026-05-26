#pragma once

#include "Honey/scene/entity.h"
#include "Honey/core/timestep.h"
#include <memory>
#include <string>

namespace Honey {
    class Scene;

    class CSharpScriptEngine {
    public:
        static void init();
        static void shutdown();

        static void on_runtime_start(Scene* scene);
        static void on_runtime_stop();

        static bool entity_class_exists(const std::string& class_name);
        static void on_create_entity(Entity entity);
        static void on_update_entity(Entity entity, Timestep ts);
        static void on_destroy_entity(Entity entity);

        static void on_collision_begin(Entity a, Entity b);
        static void on_collision_end(Entity a, Entity b);

        static Scene* get_scene_context();

        // Unloads and reloads UserScripts.dll without restarting the engine.
        static void reload_scripts();

        // Runs `dotnet build` on the user script project, then reloads. Returns true on success.
        static bool build_and_reload();

    private:
        struct Data;
        static std::unique_ptr<Data> s_data;
    };
}