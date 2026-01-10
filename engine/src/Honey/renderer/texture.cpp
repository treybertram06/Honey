#include "hnpch.h"
#include "texture.h"

#include "texture_cache.h"
#include "Honey/renderer/renderer.h"
#include "platform/opengl/opengl_texture.h"


namespace Honey {

    static TextureCache s_texture_cache;

    Ref<Texture2D> Texture2D::create(uint32_t width, uint32_t height) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLTexture2D>(width, height);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;

    }


    Ref<Texture2D> Texture2D::create(const std::string& path) {

        if (s_texture_cache.contains(path)) {
            return s_texture_cache.get(path);
        }

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return s_texture_cache.add(path, CreateRef<OpenGLTexture2D>(path));
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

}