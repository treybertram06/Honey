#pragma once
#include "Honey/core/base.h"
#include "Honey/renderer/mesh.h"

namespace Honey {

    struct GltfNode {
        std::string name;
        glm::mat4 local_transform;
        Ref<Mesh> mesh; // nullptr is the node has no geo
        std::vector<GltfNode> children;
    };

    struct GltfSceneTree {
        std::string name; // filename stem
        std::vector<GltfNode> roots;
    };

    inline const GltfNode* find_node_by_name(const GltfNode& node, const std::string& name) {
        if (node.name == name) return &node;
        for (const auto& child : node.children) {
            if (const GltfNode* found = find_node_by_name(child, name))
                return found;
        }
        return nullptr;
    }

    inline const GltfNode* find_node_by_name(const GltfSceneTree& tree, const std::string& name) {
        for (const auto& root : tree.roots) {
            if (const GltfNode* found = find_node_by_name(root, name))
                return found;
        }
        return nullptr;
    }

}
