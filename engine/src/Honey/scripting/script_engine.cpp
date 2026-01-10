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
            bool errored = false;

            sol::table properties;
        };

        std::unordered_map<UUID, ScriptInstance> entity_instances;
    };

    std::unique_ptr<ScriptEngine::ScriptEngineData> ScriptEngine::s_data = nullptr;

    static sol::table build_effective_properties(sol::state& lua, const sol::table& lua_defaults, const ScriptComponent& sc) {
        sol::table result = lua.create_table();

        if (lua_defaults.valid()) {
            for (auto& [key, value] : lua_defaults) {
                if (key.valid())
                    result[key] = value;
            }
        }

        for (const auto& [name, override_value] : sc.property_overrides) {
            std::visit([&](auto&& v) {
                result[name] = v;
            }, override_value);
        }

        return result;
    }

    void ScriptEngine::init() {
        s_data = std::make_unique<ScriptEngineData>();
        sol::state& lua = s_data->lua_state;

        lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string, sol::lib::package,
            sol::lib::debug
            );

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

        sol::table lua_defaults;
        sol::object props = instance.env["Properties"];
        if (props.valid() && props.is<sol::table>()) {
            lua_defaults = props.as<sol::table>();
        }

        instance.properties = build_effective_properties(lua, lua_defaults, sc);

        instance.instance = lua.create_table();
        instance.instance["entity"] = entity;

        UUID uuid = entity.get_uuid();
        s_data->entity_instances[uuid] = std::move(instance);

        auto& stored = s_data->entity_instances[uuid];
        if (stored.OnCreate.valid()) {

            if (!stored.errored) {

                stored.env["self"] = entity;
                if (stored.properties.valid())
                    stored.env["Properties"] = stored.properties;
                else
                    stored.env["Properties"] = sol::nil;

                sol::protected_function_result r = stored.OnCreate();

                stored.env["self"] = sol::nil;
                stored.env["Properties"] = sol::nil;

                if (!r.valid()) {
                    sol::error err = r;
                    HN_CORE_ERROR("Lua OnCreate error in '{}': {}", stored.script_name, err.what());
                    stored.errored = true;
                }
            }
        }
    }

    void ScriptEngine::on_update_entity(Entity entity, Timestep ts) {
        HN_CORE_ASSERT(s_data, "ScriptEngine not initialized!");

        UUID uuid = entity.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end()) return;

        auto& inst = it->second;
        if (!inst.valid || !inst.OnUpdate.valid() || inst.errored) return;



        HN_CORE_INFO("[Lua] Enter OnUpdate for script '{}' (entity tag='{}')",
                 inst.script_name,
                 entity.has_component<TagComponent>() ? entity.get_component<TagComponent>().tag : "<no tag>");




        inst.env["self"] = entity;
        inst.env["dt"]   = ts;
        if (inst.properties.valid())
            inst.env["Properties"] = inst.properties;
        else
            inst.env["Properties"] = sol::nil;

        //sol::protected_function_result r = inst.OnUpdate();

        sol::state& lua = s_data->lua_state;
        // Wrap OnUpdate with xpcall + debug.traceback for a clear Lua stack trace
        inst.env["__HN_LuaOnUpdateWrapper"] = inst.OnUpdate;
        lua.safe_script(R"(
        function __HN_DebugOnUpdate()
            local ok, err = xpcall(__HN_LuaOnUpdateWrapper, debug.traceback)
            if not ok then
                HN_Log("Lua OnUpdate traceback:\n" .. tostring(err))
                error(err, 0)
            end
        end
        )", inst.env);

        sol::function debug_on_update = inst.env["__HN_DebugOnUpdate"];
        sol::protected_function_result r = debug_on_update();



        inst.env["self"] = sol::nil;
        inst.env["dt"]   = sol::nil;
        inst.env["Properties"] = sol::nil;

        if (!r.valid()) {
            sol::error err = r;
            HN_CORE_ERROR("Lua OnUpdate error in '{}': {}", inst.script_name, err.what());
            inst.errored = true;
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
        if (it == s_data->entity_instances.end())
            return;

        auto& inst = it->second;

        if (!inst.valid || inst.errored || !inst.OnCollisionBegin.valid())
            return;

        inst.env["self"] = a;

        sol::protected_function_result r = inst.OnCollisionBegin(b);

        inst.env["self"] = sol::nil;

        if (!r.valid()) {
            sol::error err = r;

            HN_CORE_ERROR(
                "Lua OnCollisionBegin error in '{}' (Entity: {}): {}",
                inst.script_name,
                a.get_tag(),
                err.what()
            );

            inst.errored = true;
        }
    }

    void ScriptEngine::on_collision_end(Entity a, Entity b) {
        UUID uuid = a.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end())
            return;

        auto& inst = it->second;

        if (!inst.valid || inst.errored || !inst.OnCollisionEnd.valid())
            return;

        inst.env["self"] = a;

        sol::protected_function_result r = inst.OnCollisionEnd(b);

        inst.env["self"] = sol::nil;

        if (!r.valid()) {
            sol::error err = r;

            HN_CORE_ERROR(
                "Lua OnCollisionEnd error in '{}' (Entity: {}): {}",
                inst.script_name,
                a.get_tag(),
                err.what()
            );

            inst.errored = true;
        }
    }

    template<typename T>
    std::optional<T> ScriptEngine::get_property(Entity entity, const std::string& name) {
        if (!s_data || !entity.is_valid())
            return std::nullopt;

        UUID uuid = entity.get_uuid();
        auto it = s_data->entity_instances.find(uuid);
        if (it == s_data->entity_instances.end())
            return std::nullopt;

        const auto& instance = it->second;

        // Script failed earlier — properties are undefined
        if (instance.errored)
            return std::nullopt;

        if (!instance.properties.valid())
            return std::nullopt;

        sol::object value = instance.properties[name];
        if (!value.valid())
            return std::nullopt;

        // Type safety
        if (!value.is<T>())
            return std::nullopt;

        return value.as<T>();
    }

    // Explicit template instantiations
    template std::optional<float>       ScriptEngine::get_property<float>(Entity, const std::string&);
    template std::optional<double>      ScriptEngine::get_property<double>(Entity, const std::string&);
    template std::optional<int>         ScriptEngine::get_property<int>(Entity, const std::string&);
    template std::optional<bool>        ScriptEngine::get_property<bool>(Entity, const std::string&);
    template std::optional<std::string> ScriptEngine::get_property<std::string>(Entity, const std::string&);

    std::unordered_map<std::string, std::variant<float, bool, std::string>>
    ScriptEngine::get_default_properties(Entity entity) {
        std::unordered_map<std::string, std::variant<float, bool, std::string>> result;

        if (!s_data || !entity.is_valid())
            return result;

        auto it = s_data->entity_instances.find(entity.get_uuid());
        if (it == s_data->entity_instances.end())
            return result;

        const auto& inst = it->second;
        if (!inst.properties.valid())
            return result;

        for (auto& [key, value] : inst.properties) {
            if (!key.is<std::string>())
                continue;

            const std::string name = key.as<std::string>();

            if (value.is<float>() || value.is<double>()) {
                result[name] = value.as<float>();
            } else if (value.is<bool>()) {
                result[name] = value.as<bool>();
            } else if (value.is<std::string>()) {
                result[name] = value.as<std::string>();
            }
        }

        return result;
    }
}
