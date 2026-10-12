#pragma once

#include <glm/glm.hpp>
#include <yaml-cpp/yaml.h>

#include <cstdlib>

namespace Honey::yaml {
    // Tolerant float parse: yaml-cpp's stream-based as<float>() rejects denormals (and fails on some libc++ builds).
    // Throws YAML::TypedBadConversion<float> on genuinely invalid scalars.
    inline float read_float(const YAML::Node& node) {
        if (!node.IsScalar())
            throw YAML::TypedBadConversion<float>(node.Mark());
        const std::string& s = node.Scalar();
        char* end = nullptr;
        const float v = std::strtof(s.c_str(), &end);
        if (s.empty() || end == s.c_str() || *end != '\0')
            throw YAML::TypedBadConversion<float>(node.Mark());
        return v;
    }
}

namespace YAML {

    template<>
    struct convert<glm::vec2> {
        static Node encode(const glm::vec2& rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            return node;
        }

        static bool decode(const Node& node, glm::vec2& rhs) {
            if (!node.IsSequence() || node.size() != 2)
                return false;

            rhs.x = Honey::yaml::read_float(node[0]);
            rhs.y = Honey::yaml::read_float(node[1]);
            return true;
        }
    };

    template<>
    struct convert<glm::vec3> {
        static Node encode(const glm::vec3& rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& rhs) {
            if (!node.IsSequence() || node.size() != 3)
                return false;

            rhs.x = Honey::yaml::read_float(node[0]);
            rhs.y = Honey::yaml::read_float(node[1]);
            rhs.z = Honey::yaml::read_float(node[2]);
            return true;
        }
    };

    template<>
    struct convert<glm::vec4> {
        static Node encode(const glm::vec4& rhs) {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            node.push_back(rhs.w);
            return node;
        }

        static bool decode(const Node& node, glm::vec4& rhs) {
            if (!node.IsSequence() || node.size() != 4)
                return false;

            rhs.x = Honey::yaml::read_float(node[0]);
            rhs.y = Honey::yaml::read_float(node[1]);
            rhs.z = Honey::yaml::read_float(node[2]);
            rhs.w = Honey::yaml::read_float(node[3]);
            return true;
        }
    };

} // namespace YAML