#include "shader.h"

#include <glad/glad.h>

#include <glm/gtc/type_ptr.hpp>

namespace Honey {
    Shader::Shader(const std::string &vertex_src, const std::string &fragment_src) {

		// Create an empty vertex shader handle
		GLuint vertex_shader = glCreateShader(GL_VERTEX_SHADER);

    	// Send the vertex shader source code to GL
    	// Note that std::string's .c_str is NULL character terminated.
    	const GLchar *source = vertex_src.c_str();
    	glShaderSource(vertex_shader, 1, &source, 0);

    	// Compile the vertex shader
    	glCompileShader(vertex_shader);

    	GLint is_compiled = 0;
    	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &is_compiled);
    	if(is_compiled == GL_FALSE)
    	{
    		GLint max_length = 0;
    		glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &max_length);

    		// The maxLength includes the NULL character
    		std::vector<GLchar> info_log(max_length);
    		glGetShaderInfoLog(vertex_shader, max_length, &max_length, &info_log[0]);

    		// We don't need the shader anymore.
    		glDeleteShader(vertex_shader);

    		HN_CORE_ERROR("{0}", info_log.data());
    		HN_CORE_ASSERT(false, "Vertex shader compilation error!");
    		return;
    	}

    	// Create an empty fragment shader handle
    	GLuint fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

    	// Send the fragment shader source code to GL
    	// Note that std::string's .c_str is NULL character terminated.
    	source = (const GLchar *)fragment_src.c_str();
    	glShaderSource(fragment_shader, 1, &source, 0);

    	// Compile the fragment shader
    	glCompileShader(fragment_shader);

    	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &is_compiled);
    	if (is_compiled == GL_FALSE)
    	{
    		GLint max_length = 0;
    		glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &max_length);

    		// The maxLength includes the NULL character
    		std::vector<GLchar> info_log(max_length);
    		glGetShaderInfoLog(fragment_shader, max_length, &max_length, &info_log[0]);

    		// We don't need the shader anymore.
    		glDeleteShader(fragment_shader);
    		// Either of them. Don't leak shaders.
    		glDeleteShader(vertex_shader);

    		HN_CORE_ERROR("{0}", info_log.data());
    		HN_CORE_ASSERT(false, "Fragment shader compilation error!");
    		return;
    	}

    	// Vertex and fragment shaders are successfully compiled.
    	// Now time to link them together into a program.
    	// Get a program object.
    	m_renderer_id = glCreateProgram();
    	GLuint program = m_renderer_id;

    	// Attach our shaders to our program
    	glAttachShader(program, vertex_shader);
    	glAttachShader(program, fragment_shader);

    	// Link our program
    	glLinkProgram(program);

    	// Note the different functions here: glGetProgram* instead of glGetShader*.
    	GLint is_linked = 0;
    	glGetProgramiv(program, GL_LINK_STATUS, (int *)&is_linked);
    	if (is_linked == GL_FALSE)
    	{
    		GLint max_length = 0;
    		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &max_length);

    		// The maxLength includes the NULL character
    		std::vector<GLchar> info_log(max_length);
    		glGetProgramInfoLog(program, max_length, &max_length, &info_log[0]);

    		// We don't need the program anymore.
    		glDeleteProgram(program);
    		// Don't leak shaders either.
    		glDeleteShader(vertex_shader);
    		glDeleteShader(fragment_shader);

    		HN_CORE_ERROR("{0}", info_log.data());
    		HN_CORE_ASSERT(false, "Shader link error!");
    		return;
    	}

    	// Always detach shaders after a successful link.
    	glDetachShader(program, vertex_shader);
    	glDetachShader(program, fragment_shader);
    }

    Shader::~Shader() {
	    glDeleteProgram(m_renderer_id);
    }

    void Shader::bind() const {
		glUseProgram(m_renderer_id);
    }

    void Shader::unbind() const {
		glUseProgram(0);
    }

    void Shader::upload_uniform_mat4(const std::string& name, const glm::mat4 &matrix) {
    	GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
		if (location != -1)
    		glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
    	else
    		HN_CORE_ASSERT(false, "Mat4 uniform not found!");
    }

	void Shader::upload_uniform_float4(const std::string& name, const glm::vec4& vec) {
		GLint location = glGetUniformLocation(m_renderer_id, name.c_str());
    	if (location != -1)
    		glUniform4f(location, vec.x, vec.y, vec.z, vec.w);
    	else
    		HN_CORE_ASSERT(false, "Float4 uniform not found!");
	}


}
