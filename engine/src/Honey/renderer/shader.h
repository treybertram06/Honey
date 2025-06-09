#pragma once

#include <string>

namespace Honey {
    class Shader {
    public:
        virtual ~Shader() = default;

        virtual void bind() const = 0;
        virtual void unbind() const = 0;

        virtual const std::string& get_name() const = 0;

        static Ref<Shader> create(const std::string& path);
        static Ref<Shader> create(const std::string& name, const std::string& vertex_src, const std::string& fragment_src);
    };

    class ShaderLibrary {
    public:
        void add(const Ref<Shader>& shader);
        void add(const std::string& name, const Ref<Shader>& shader);
        Ref<Shader> load(const std::string& path);
        Ref<Shader> load(const std::string& name, const std::string& path);

        bool exists(const std::string& name);

        Ref<Shader> get(const std::string& name);
    private:
        std::unordered_map<std::string, Ref<Shader>> m_shaders;
    };
}
