#include "hnpch.h"
#include "shader.h"

#include "renderer.h"
#include "platform/opengl/opengl_shader.h"

namespace Honey {

    Shader* Shader::create(const std::string& path) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return new OpenGLShader(path);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Shader* Shader::create(const std::string &vertex_src, const std::string &fragment_src) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return new OpenGLShader(vertex_src, fragment_src);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }




}
