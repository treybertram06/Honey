#pragma once

#include <string>
#include <unordered_map>
#include <variant>

namespace Honey {

    using ScriptPropertyValue = std::variant<float, bool, std::string>;

    class ScriptPropertiesWriter {
    public:
        static bool write_defaults(
            const std::string& script_name,
            const std::unordered_map<std::string, ScriptPropertyValue>& values,
            std::string* out_error = nullptr
        );
    };

}