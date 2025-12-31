#pragma once

#include <string>
#include <unordered_map>
#include <variant>

namespace Honey {

    using ScriptPropertyValue = std::variant<
        float,
        bool,
        std::string
    >;

    using ScriptPropertyMap = std::unordered_map<std::string, ScriptPropertyValue>;

    class ScriptPropertiesLoader {
    public:
        static ScriptPropertyMap load_from_file(const std::string& script_name);

        static void invalidate_cache(const std::string& script_name);
        static void invalidate_all();
    };

}