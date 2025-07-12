#include "hnpch.h"
#include "opengl_shader.h"

#include <glad/glad.h>
#include <fstream>

#include <glm/gtc/type_ptr.hpp>

namespace Honey {

    static GLenum shader_type_from_str(const std::string& type) {

        if (type == "vertex")
            return GL_VERTEX_SHADER;
        if (type == "fragment" || type == "pixel")
            return GL_FRAGMENT_SHADER;

        HN_CORE_ASSERT(false, "Unknown shader type!");
        return 0;
    }


    OpenGLShader::OpenGLShader(const std::string& path) {
        HN_PROFILE_FUNCTION();

        std::string source = read_file(path);
        auto shader_srcs = pre_process(source);
        compile(shader_srcs);

        auto last_slash = path.find_last_of("/\\");
        last_slash = last_slash == std::string::npos ? 0 : last_slash + 1;
        auto last_dot = path.rfind('.');
        auto count = last_dot == std::string::npos ? path.size() - last_slash : last_dot - last_slash;
        m_name = path.substr(last_slash, count);
    }
    OpenGLShader::OpenGLShader(const std::string& name, const std::string &vertex_src, const std::string &fragment_src)
        : m_name(name) {
        std::unordered_map<GLenum, std::string> sources;
        sources[GL_VERTEX_SHADER] = vertex_src;
        sources[GL_FRAGMENT_SHADER] = fragment_src;
        compile(sources);
    }

    OpenGLShader::~OpenGLShader() {
        HN_PROFILE_FUNCTION();

	    glDeleteProgram(m_renderer_id);
    }

    void OpenGLShader::compile(const std::unordered_map<GLenum, std::string> &shader_srcs) {
        HN_PROFILE_FUNCTION();

        GLuint program = glCreateProgram();
        HN_CORE_ASSERT(shader_srcs.size() <= 2, "Only supports 2 shaders for now");
        std::array<GLenum, 2> gl_shader_ids;
        int gl_shader_index = 0;

        for (auto&& [shader_type, source] : shader_srcs) {

            GLuint shader = glCreateShader(shader_type);

            const GLchar *sourcecstr = source.c_str();
            glShaderSource(shader, 1, &sourcecstr, 0);

            glCompileShader(shader);

            GLint is_compiled = 0;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &is_compiled);
            if(is_compiled == GL_FALSE)
            {
                GLint max_length = 0;
                glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &max_length);

                std::vector<GLchar> info_log(max_length);
                glGetShaderInfoLog(shader, max_length, &max_length, &info_log[0]);

                glDeleteShader(shader);

                HN_CORE_ERROR("{0}", info_log.data());
                HN_CORE_ASSERT(false, "Shader compilation error!");
                break;
            }
            glAttachShader(program, shader);
            gl_shader_ids[gl_shader_index++] = shader;


        }
    	glLinkProgram(program);

    	GLint is_linked = 0;
    	glGetProgramiv(program, GL_LINK_STATUS, (int *)&is_linked);
    	if (is_linked == GL_FALSE)
    	{
    		GLint max_length = 0;
    		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &max_length);

    	    std::vector<GLchar> info_log(max_length);
    		glGetProgramInfoLog(program, max_length, &max_length, &info_log[0]);

    		glDeleteProgram(program);
    	    for (auto id : gl_shader_ids)
    	        glDeleteShader(id);

    		HN_CORE_ERROR("{0}", info_log.data());
    		HN_CORE_ASSERT(false, "Shader link error!");
    		return;
    	}
        for (auto id : gl_shader_ids)
            glDetachShader(program, id);

        m_renderer_id = program;
    }


    std::string OpenGLShader::read_file(const std::string& path) {
        HN_PROFILE_FUNCTION();

        std::string result;
        std::ifstream in(path, std::ios::in | std::ios::binary);
        if (in) {
            in.seekg(0, std::ios::end);
            result.resize(in.tellg());
            in.seekg(0, std::ios::beg);
            in.read(&result[0], result.size());
            in.close();
        } else {
            HN_CORE_ERROR("Could not open shader at path: {0}", path);
        }
        return result;
    }

    std::unordered_map<GLenum, std::string> OpenGLShader::pre_process(const std::string& source) {
        HN_PROFILE_FUNCTION();

        std::unordered_map<GLenum, std::string> shader_srcs;

        const char* type_token = "#type";
        size_t type_token_len = strlen(type_token);
        size_t pos = source.find(type_token, 0);
        while (pos != std::string::npos) {
            size_t eol = source.find_first_of("\r\n", pos);
            HN_CORE_ASSERT(eol != std::string::npos, "Syntax error.");
            size_t begin = pos + type_token_len + 1;
            std::string type = source.substr(begin, eol - begin);
            HN_CORE_ASSERT(shader_type_from_str(type), "Invalid shader type.");

            size_t next_line_pos = source.find_first_not_of("\r\n", eol);
            pos = source.find(type_token, next_line_pos);
            shader_srcs[shader_type_from_str(type)] =
                source.substr(next_line_pos,
                    pos - (next_line_pos == std::string::npos ? source.size() - 1 : next_line_pos));
        }
        return shader_srcs;
    }


    void OpenGLShader::bind() const {
        HN_PROFILE_FUNCTION();

		glUseProgram(m_renderer_id);
    }

    void OpenGLShader::unbind() const {
        HN_PROFILE_FUNCTION();

		glUseProgram(0);
    }

    void OpenGLShader::set_float(const std::string& name, float value) {
        HN_PROFILE_FUNCTION();

        upload_uniform_float(name, value);
    }

    void OpenGLShader::set_float2(const std::string& name, const glm::vec2 &value) {
        HN_PROFILE_FUNCTION();

        upload_uniform_float2(name, value);
    }

    void OpenGLShader::set_float3(const std::string& name, const glm::vec3 &value) {
        HN_PROFILE_FUNCTION();

        upload_uniform_float3(name, value);
    }

    void OpenGLShader::set_float4(const std::string& name, const glm::vec4 &value) {
        HN_PROFILE_FUNCTION();

        upload_uniform_float4(name, value);
    }

    void OpenGLShader::set_mat4(const std::string& name, const glm::mat4 &value) {
        HN_PROFILE_FUNCTION();

        upload_uniform_mat4(name, value);
    }

    void OpenGLShader::set_int(const std::string &name, int value) {
        HN_PROFILE_FUNCTION();

        upload_uniform_int(name, value);
    }

    void OpenGLShader::set_int_array(const std::string &name, int* values, uint32_t count) {
        HN_PROFILE_FUNCTION();

        upload_uniform_int_array(name, values, count);
    }


    void OpenGLShader::upload_uniform_mat4(const std::string& name, const glm::mat4& matrix) {
    	GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
		if (location != -1)
    		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    	else {
    	    HN_CORE_ERROR("At location: {0}", name);
    	    HN_CORE_ASSERT(false, "Mat4 uniform not found!");
    	}
    }

    void OpenGLShader::upload_uniform_mat3(const std::string& name, const glm::mat3& matrix) {
        GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
        if (location != -1)
            glUniformMatrix3fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
        else {
            HN_CORE_ERROR("At location: {0}", name);
            HN_CORE_ASSERT(false, "Mat3 uniform not found!");
        }
    }

	void OpenGLShader::upload_uniform_float4(const std::string& name, const glm::vec4& value) {
		GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
    	if (location != -1)
    		glUniform4f(location, value.x, value.y, value.z, value.w);
    	else {
    	    HN_CORE_ERROR("At location: {0}", name);
    	    HN_CORE_ASSERT(false, "Float4 uniform not found!");
    	}
	}

    void OpenGLShader::upload_uniform_float3(const std::string& name, const glm::vec3& value) {
        GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
        if (location != -1)
            glUniform3f(location, value.x, value.y, value.z);
        else {
            HN_CORE_ERROR("At location: {0}", name);
            HN_CORE_ASSERT(false, "Float3 uniform not found!");
        }
    }

    void OpenGLShader::upload_uniform_float2(const std::string& name, const glm::vec2& value) {
        GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
        if (location != -1)
            glUniform2f(location, value.x, value.y);
        else {
            HN_CORE_ERROR("At location: {0}", name);
            HN_CORE_ASSERT(false, "Float2 uniform not found!");
        }
    }

    void OpenGLShader::upload_uniform_float(const std::string& name, float value) {
        GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
        if (location != -1)
            glUniform1f(location, value);
        else {
            HN_CORE_ERROR("At location: {0}", name);
            HN_CORE_ASSERT(false, "Float uniform not found!");
        }
    }

    void OpenGLShader::upload_uniform_int(const std::string& name, int value) {
        GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
        if (location != -1)
            glUniform1i(location, value);
        else {
            HN_CORE_ERROR("At location: {0}", name);
            HN_CORE_ASSERT(false, "Integer uniform not found!");
        }
    }

    void OpenGLShader::upload_uniform_int_array(const std::string& name, int* values, uint32_t count) {
        GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
        if (location != -1)
            glUniform1iv(location, count, values);
        else {
            HN_CORE_ERROR("At location: {0}", name);
            HN_CORE_ASSERT(false, "Integer uniform not found!");
        }
    }



}
