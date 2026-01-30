#include "hnpch.h"
#include "Honey/renderer/texture_cache.h"

#include <filesystem>

namespace Honey {

    bool TextureCache::contains(const std::string& path) const {
        auto canonical_path = std::filesystem::canonical(path).string();
        return m_texture_map.find(canonical_path) != m_texture_map.end();
    }

    Ref<Texture2D> TextureCache::get(const std::string& path) const {
        auto canonical_path = std::filesystem::canonical(path).string();

        auto it = m_texture_map.find(canonical_path);
        if (it != m_texture_map.end())
            return it->second;

        return nullptr;
    }

    Ref<Texture2D> TextureCache::add(const std::string& path, const Ref<Texture2D>& texture) {
        auto canonical_path = std::filesystem::canonical(path).string();

        m_texture_map[canonical_path] = texture;
        return texture;
    }

    void TextureCache::clear() {
        m_texture_map.clear();
    }

    void TextureCache::recreate_all_samplers() {
        for (auto& [path, texture] : m_texture_map) {
            if (texture) {
                texture->refresh_sampler();
            }
        }
    }

} // namespace Honey