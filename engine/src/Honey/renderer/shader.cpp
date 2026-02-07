#include "hnpch.h"
#include "shader.h"

#include "renderer.h"
#include "platform/opengl/opengl_shader.h"
#include "Honey/renderer/shader_cache.h"

#include <filesystem>
#include <fstream>

namespace Honey {

    Ref<Shader> Shader::create(const std::filesystem::path& path) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return std::make_shared<OpenGLShader>(path.generic_string());
            case RendererAPI::API::vulkan:   return nullptr;
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<Shader> Shader::create(const std::string& name, const std::string &vertex_src, const std::string &fragment_src) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return std::make_shared<OpenGLShader>(name, vertex_src, fragment_src);
            case RendererAPI::API::vulkan:   return nullptr;
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<Shader> Shader::create_from_spirv(const std::string& name,
                                          const std::vector<uint32_t>& vertex_spirv,
                                          const std::vector<uint32_t>& fragment_spirv)
    {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:   HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl: return std::make_shared<OpenGLShader>(name, vertex_spirv, fragment_spirv);
            case RendererAPI::API::vulkan:   return nullptr;
        }
        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    static std::vector<uint32_t> read_spirv_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return {};
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);
        std::vector<uint32_t> buffer((size_t)size / sizeof(uint32_t));
        if (!file.read(reinterpret_cast<char*>(buffer.data()), size))
            return {};
        return buffer;
    }

    Ref<Shader> Shader::create_from_spirv_files(const std::filesystem::path& vertex_spirv_path,
                                                const std::filesystem::path& fragment_spirv_path)
    {
        auto vert = read_spirv_file(vertex_spirv_path);
        auto frag = read_spirv_file(fragment_spirv_path);
        HN_CORE_ASSERT(!vert.empty() && !frag.empty(), "Failed to read SPIR-V files");
        std::string name = vertex_spirv_path.stem().string();
        return create_from_spirv(name, vert, frag);
    }

    Ref<Shader> Shader::create_with_auto_compile(const std::filesystem::path& glsl_path)
    {
        return Renderer::get_shader_cache()->get_or_compile_shader(glsl_path);
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
