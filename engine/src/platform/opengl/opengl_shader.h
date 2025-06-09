#pragma once

#include "Honey/renderer/shader.h"
#include "glad/glad.h"
#include "glm/glm.hpp"
#include "hnpch.h"

namespace Honey {
    class OpenGLShader : public Shader {
    public:
        OpenGLShader(const std::string& path);
        OpenGLShader(const std::string& name, const std::string& vertex_src, const std::string& fragment_src);
        virtual ~OpenGLShader();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual const std::string& get_name() const override { return m_name; }

        void upload_uniform_mat4(const std::string& name, const glm::mat4& matrix);
        void upload_uniform_mat3(const std::string& name, const glm::mat3& matrix);

        void upload_uniform_float4(const std::string& name, const glm::vec4& value);
        void upload_uniform_float3(const std::string& name, const glm::vec3& value);
        void upload_uniform_float2(const std::string& name, const glm::vec2& value);
        void upload_uniform_float(const std::string& name, float value);

        void upload_uniform_int(const std::string& name, int value);

    private:
        std::string read_file(const std::string& path);
        std::unordered_map<GLenum, std::string> pre_process(const std::string& source);
        void compile(const std::unordered_map<GLenum, std::string>& shader_srcs);

        uint32_t m_renderer_id;
        std::string m_name;
    };
}
