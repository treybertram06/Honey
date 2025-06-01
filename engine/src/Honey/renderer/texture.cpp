#include "hnpch.h"
#include "texture.h"

#include "Honey/renderer/renderer.h"
#include "platform/opengl/opengl_texture.h"


namespace Honey {

    Ref<Texture2D> Texture2D::create(const std::string& path) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return std::make_shared<OpenGLTexture2D>(path);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

}