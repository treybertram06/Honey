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

        virtual void set_float(const std::string& name, float value) override;
        virtual void set_float2(const std::string& name, const glm::vec2& value) override;
        virtual void set_float3(const std::string& name, const glm::vec3& value) override;
        virtual void set_float4(const std::string& name, const glm::vec4& value) override;
        virtual void set_mat4(const std::string& name, const glm::mat4& value) override;
        virtual void set_int(const std::string& name, int value) override;
        virtual void set_int_array(const std::string& name, int* values, uint32_t count) override;

        virtual const std::string& get_name() const override { return m_name; }

        void upload_uniform_mat4(const std::string& name, const glm::mat4& matrix);
        void upload_uniform_mat3(const std::string& name, const glm::mat3& matrix);

        void upload_uniform_float4(const std::string& name, const glm::vec4& value);
        void upload_uniform_float3(const std::string& name, const glm::vec3& value);
        void upload_uniform_float2(const std::string& name, const glm::vec2& value);
        void upload_uniform_float(const std::string& name, float value);

        void upload_uniform_int(const std::string& name, int value);
        void upload_uniform_int_array(const std::string& name, int* values, uint32_t count);

    private:
        std::string read_file(const std::string& path);
        std::unordered_map<GLenum, std::string> pre_process(const std::string& source);
        void compile(const std::unordered_map<GLenum, std::string>& shader_srcs);

        uint32_t m_renderer_id;
        std::string m_name;
    };
}
