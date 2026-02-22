#pragma once
#include <unordered_map>

#include "texture.h"

namespace Honey {

    class TextureCache {
    public:
        // Static singleton accessor
        static TextureCache& get() {
            // Use the same instance as Texture2D::create()
            extern TextureCache& texture_cache_instance();
            return Texture2D::texture_cache_instance();
        }

        bool contains(const std::string& path) const;
        Ref<Texture2D> get(const std::string& path) const;
        Ref<Texture2D> add(const std::string& path, const Ref<Texture2D>& texture);
        void clear();
        void recreate_all_samplers();

    private:
        std::unordered_map<std::string, Ref<Texture2D>> m_texture_map;
    };

} // namespace Honey