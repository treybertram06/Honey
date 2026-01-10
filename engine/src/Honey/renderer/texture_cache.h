#pragma once
#include <unordered_map>

#include "texture.h"

namespace Honey {

    class TextureCache {
    public:
        bool contains(const std::string& path) const {
            auto canonical_path = std::filesystem::canonical(path);

            return m_texture_map.find(canonical_path) != m_texture_map.end();
        }

        Ref<Texture2D> get(const std::string& path) const {
            auto canonical_path = std::filesystem::canonical(path);

            auto it = m_texture_map.find(canonical_path);
            if (it != m_texture_map.end()) {
                return it->second;
            }
            return nullptr;
        }

        Ref<Texture2D> add(const std::string& path, const Ref<Texture2D>& texture) {
            auto canonical_path = std::filesystem::canonical(path);

            m_texture_map[canonical_path] = texture;
            return texture;
        }

    private:
        std::unordered_map<std::string, Ref<Texture2D>> m_texture_map;
    };
}
