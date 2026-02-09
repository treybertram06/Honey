#include "hnpch.h"
#include "Honey/renderer/texture_cache.h"

#include <filesystem>

namespace Honey {

    namespace {
        static std::string normalize_texture_path(const std::string& path) {
            namespace fs = std::filesystem;

            std::error_code ec;

            // If the file doesn't exist (e.g. while moving/renaming), avoid throwing.
            // weakly_canonical() resolves what it can without requiring every component to exist.
            fs::path p(path);
            fs::path normalized = fs::weakly_canonical(p, ec);
            if (!ec && !normalized.empty())
                return normalized.string();

            // Fallback: absolute path (also non-throwing). Still gives a stable-ish key.
            ec.clear();
            normalized = fs::absolute(p, ec);
            if (!ec && !normalized.empty())
                return normalized.string();

            // Last resort: keep original string (prevents crashes, but key may be inconsistent).
            return path;
        }
    }

    bool TextureCache::contains(const std::string& path) const {
        auto canonical_path =  normalize_texture_path(path);
        return m_texture_map.find(canonical_path) != m_texture_map.end();
    }

    Ref<Texture2D> TextureCache::get(const std::string& path) const {
        auto canonical_path = normalize_texture_path(path);

        auto it = m_texture_map.find(canonical_path);
        if (it != m_texture_map.end())
            return it->second;

        return nullptr;
    }

    Ref<Texture2D> TextureCache::add(const std::string& path, const Ref<Texture2D>& texture) {
        auto canonical_path = normalize_texture_path(path);

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