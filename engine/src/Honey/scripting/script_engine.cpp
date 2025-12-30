#include "hnpch.h"
#include "script_engine.h"

#include "script_glue.h"
#include "Honey/scene/scene.h"
#include "Honey/scene/entity.h"
#include "Honey/scene/components.h"
#include <sol/sol.hpp>
#include <filesystem>

namespace Honey {

    struct ScriptEngine::ScriptEngineData {
        sol::state lua_state;
        Scene* scene_context = nullptr;
        std::filesystem::path script_root = "../assets/scripts";

        struct ScriptInstance {
            std::string script_name;
            sol::environment env;
            sol::table instance;
            sol::function OnCreate;
            sol::function OnUpdate;
            sol::function OnDestroy;
            sol::function OnCollisionBegin;
            sol::function OnCollisionEnd;
            bool valid = false;
        };

        std::unordered_map<UUID, ScriptInstance> entity_instances;
    };

    std::unique_ptr<ScriptEngine::ScriptEngineData> ScriptEngine::s_data = nullptr;

    void ScriptEngine::init() {
        s_data = std::make_unique<ScriptEngineData>();
        sol::state& lua = s_data->lua_state;

        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::package);

        lua.set_function("HN_Log", [](const std::string& msg) {
            HN_CORE_INFO("[Lua] {}", msg);
        });

        ScriptGlue::register_functions();
    }

    void ScriptEngine::shutdown() {
        s_data.reset();
    }

    sol::state& ScriptEngine::get_lua_state() {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");
        return s_data->lua_state;
    }

    Scene* ScriptEngine::get_scene_context() {
        return s_data ? s_data->scene_context : nullptr;
    }

    void ScriptEngine::on_runtime_start(Scene* scene) {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");
        s_data->scene_context = scene;
    }

    void ScriptEngine::on_runtime_stop() {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");
        s_data->scene_context = nullptr;
        s_data->entity_instances.clear();
    }

    bool ScriptEngine::entity_class_exists(const std::string& full_class_name) {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");
        std::filesystem::path scriptPath = s_data->script_root / (full_class_name + ".lua");
        return std::filesystem::exists(scriptPath);
    }

    void ScriptEngine::on_create_entity(Entity entity) {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");

        auto& sc = entity.get_component<ScriptComponent>();
        const std::string& scriptName = sc.script_name;
        std::filesystem::path scriptPath = s_data->script_root / (scriptName + ".lua");

        if (!std::filesystem::exists(scriptPath)) {
            HN_CORE_ERROR("Lua script '{}' not found at '{}'", scriptName, scriptPath.string());
            return;
        }

        ScriptEngineData::ScriptInstance instance;
        instance.script_name = scriptName;

        sol::state& lua = s_data->lua_state;
        instance.env = sol::environment(lua, sol::create, lua.globals());

        sol::protected_function_result loadResult = lua.safe_script_file(scriptPath.string(), instance.env, &sol::script_pass_on_error);
        if (!loadResult.valid()) {
            sol::error err = loadResult;
            HN_CORE_ERROR("Error loading Lua script '{}': {}", scriptPath.string(), err.what());
            return;
        }

        instance.OnCreate        = instance.env["OnCreate"];
        instance.OnUpdate        = instance.env["OnUpdate"];
        instance.OnDestroy       = instance.env["OnDestroy"];
        instance.OnCollisionBegin = instance.env["OnCollisionBegin"];
        instance.OnCollisionEnd   = instance.env["OnCollisionEnd"];
        instance.valid = true;

        instance.instance = lua.create_table();
        instance.instance["entity"] = entity;

        UUID uuid = entity.get_uuid();
        s_data->entity_instances[uuid] = std::move(instance);

        auto& stored = s_data->entity_instances[uuid];
        if (stored.OnCreate.valid()) {
            sol::protected_function_result r = stored.OnCreate(stored.instance);
            if (!r.valid()) {
                sol::error err = r;
                HN_CORE_ERROR("Lua OnCreate error in '{}': {}", stored.script_name, err.what());
            }
        }
    }

    void ScriptEngine::on_update_entity(Entity entity, Timestep ts) {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");

        UUID uuid = entity.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end()) return;

        auto& inst = it->second;
        if (!inst.valid || !inst.OnUpdate.valid()) return;

        sol::protected_function_result r = inst.OnUpdate(inst.instance, (float)ts);
        if (!r.valid()) {
            sol::error err = r;
            HN_CORE_ERROR("Lua OnUpdate error in '{}': {}", inst.script_name, err.what());
        }
    }

    void ScriptEngine::on_destroy_entity(Entity entity) {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");

        UUID uuid = entity.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end()) return;

        auto& inst = it->second;
        if (inst.valid && inst.OnDestroy.valid()) {
            sol::protected_function_result r = inst.OnDestroy(inst.instance);
            if (!r.valid()) {
                sol::error err = r;
                HN_CORE_ERROR("Lua OnDestroy error in '{}': {}", inst.script_name, err.what());
            }
        }

        s_data->entity_instances.erase(it);
    }

    void ScriptEngine::on_collision_begin(Entity a, Entity b) {
        UUID uuid = a.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end()) return;

        auto& inst = it->second;
        if (inst.valid && inst.OnCollisionBegin.valid()) {
            sol::protected_function_result r = inst.OnCollisionBegin(inst.instance, b);
            if (!r.valid()) {
                sol::error err = r;
                HN_CORE_ERROR("Lua OnCollisionBegin error in '{}': {}", inst.script_name, err.what());
            }
        }
    }

    void ScriptEngine::on_collision_end(Entity a, Entity b) {
        UUID uuid = a.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end()) return;

        auto& inst = it->second;
        if (inst.valid && inst.OnCollisionEnd.valid()) {
            sol::protected_function_result r = inst.OnCollisionEnd(inst.instance, b);
            if (!r.valid()) {
                sol::error err = r;
                HN_CORE_ERROR("Lua OnCollisionEnd error in '{}': {}", inst.script_name, err.what());
            }
        }
    }

}
