#include "hnpch.h"
#include "texture.h"

#include "Honey/renderer/renderer.h"
#include "platform/metal/metal_texture.h"
#include "platform/opengl/opengl_texture.h"


namespace Honey {

    Ref<Texture2D> Texture2D::create(std::uint32_t width, std::uint32_t height) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLTexture2D>(width, height);
            case RendererAPI::API::metal:   return CreateRef<MetalTexture2D>(width, height);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;

    }


    Ref<Texture2D> Texture2D::create(const std::string& path) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLTexture2D>(path);
            case RendererAPI::API::metal:   return CreateRef<MetalTexture2D>(path);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

}