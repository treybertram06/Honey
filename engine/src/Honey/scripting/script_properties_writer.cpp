#include "script_properties_writer.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Honey {

    static std::string value_to_lua(const ScriptPropertyValue& v) {
        return std::visit([](auto&& val) -> std::string {
            using T = std::decay_t<decltype(val)>;

            if constexpr (std::is_same_v<T, float>) {
                return std::to_string(val);
            } else if constexpr (std::is_same_v<T, bool>) {
                return val ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::string>) {
                return "\"" + val + "\"";
            }
        }, v);
    }

    bool ScriptPropertiesWriter::write_defaults(
        const std::string& script_name,
        const std::unordered_map<std::string, ScriptPropertyValue>& values,
        std::string* out_error
    ) {
        std::filesystem::path path = "../assets/scripts";
        path /= script_name;
        path.replace_extension(".lua");

        std::ifstream in(path);
        if (!in.is_open()) {
            if (out_error) *out_error = "Failed to open Lua script";
            return false;
        }

        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string source = buffer.str();

        const std::string marker = "Properties";
        size_t props_pos = source.find(marker);
        if (props_pos == std::string::npos) {
            if (out_error) *out_error = "No Properties table found";
            return false;
        }

        size_t open_brace = source.find("{", props_pos);
        size_t close_brace = source.find("}", open_brace);
        if (open_brace == std::string::npos || close_brace == std::string::npos) {
            if (out_error) *out_error = "Malformed Properties block";
            return false;
        }

        std::ostringstream new_props;
        new_props << "Properties = {\n";

        for (auto& [name, value] : values) {
            new_props << "    " << name << " = " << value_to_lua(value) << ",\n";
        }

        new_props << "}";

        source.replace(
            source.begin() + props_pos,
            source.begin() + close_brace + 1,
            new_props.str()
        );

        std::ofstream out(path, std::ios::trunc);
        out << source;

        return true;
    }

}