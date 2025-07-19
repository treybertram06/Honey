#include "hnpch.h"
#include "shader.h"

#include "renderer.h"
#include "platform/metal/metal_shader.h"
#include "platform/opengl/opengl_shader.h"

namespace Honey {

    Ref<Shader> Shader::create(const std::string& path) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return std::make_shared<OpenGLShader>(path);
            case RendererAPI::API::metal:   return std::make_shared<MetalShader>(path);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<Shader> Shader::create(const std::string& name, const std::string &vertex_src, const std::string &fragment_src) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLShader>(name, vertex_src, fragment_src);
            case RendererAPI::API::metal:   return CreateRef<MetalShader>(name, vertex_src, fragment_src);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    void ShaderLibrary::add(const std::string& name, const Ref<Shader>& shader) {
        HN_CORE_ASSERT(!exists(name), "Shader already exists!");
        m_shaders[name] = shader;
    }

    void ShaderLibrary::add(const Ref<Shader> &shader) {
        auto& name = shader->get_name();
        add(name, shader);
    }

    Ref<Shader> ShaderLibrary::load(const std::string &path) {
        auto shader = Shader::create(path);
        add(shader);
        return shader;
    }

    Ref<Shader> ShaderLibrary::load(const std::string &name, const std::string &path) {
        auto shader = Shader::create(path);
        add(name, shader);
        return shader;
    }

    Ref<Shader> ShaderLibrary::get(const std::string &name) {

        HN_CORE_ASSERT(exists(name), "Shader not found!");
        return m_shaders[name];
    }

    bool ShaderLibrary::exists(const std::string &name) {
        return m_shaders.find(name) != m_shaders.end();
    }

}
