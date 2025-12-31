#include "script_properties_loader.h"

#include <sol/sol.hpp>
#include <filesystem>

namespace Honey {

    static std::unordered_map<std::string, ScriptPropertyMap> s_cache;

    ScriptPropertyMap ScriptPropertiesLoader::load_from_file(const std::string& script_name) {
        auto it = s_cache.find(script_name);
        if (it != s_cache.end())
            return it->second;

        ScriptPropertyMap result;

        if (script_name.empty())
            return result;

        std::filesystem::path path = "../assets/scripts";
        path /= script_name;
        path.replace_extension(".lua");

        if (!std::filesystem::exists(path))
            return result;

        sol::state lua;
        lua.open_libraries(sol::lib::base);

        sol::protected_function_result load = lua.safe_script_file(
            path.string(),
            sol::script_pass_on_error
        );

        if (!load.valid())
            return result;

        sol::object props = lua["Properties"];
        if (!props.valid() || !props.is<sol::table>())
            return result;

        sol::table table = props.as<sol::table>();

        for (auto& [key, value] : table) {
            if (!key.is<std::string>())
                continue;

            const std::string name = key.as<std::string>();

            if (value.is<float>() || value.is<double>()) {
                result[name] = value.as<float>();
            }
            else if (value.is<bool>()) {
                result[name] = value.as<bool>();
            }
            else if (value.is<std::string>()) {
                result[name] = value.as<std::string>();
            }
        }

        s_cache[script_name] = result;
        return result;
    }

    void ScriptPropertiesLoader::invalidate_cache(const std::string& script_name) {
        s_cache.erase(script_name);
    }

    void ScriptPropertiesLoader::invalidate_all() {
        s_cache.clear();
    }

}