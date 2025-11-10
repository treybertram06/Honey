#include "hnpch.h"
#include "opengl_buffer.h"

#include <glad/glad.h>

#include "spdlog/fmt/bundled/chrono.h"

namespace Honey {

    // Vertex Buffer //////////////////////////////////////////////////////

    OpenGLVertexBuffer::OpenGLVertexBuffer(uint32_t size) {
        HN_PROFILE_FUNCTION();

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
        glCreateBuffers(1, &m_renderer_id);
#endif
#ifdef HN_PLATFORM_MACOS
        glGenBuffers(1, &m_renderer_id);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, m_renderer_id);
        glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    }

    OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size) {
        HN_PROFILE_FUNCTION();

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
        glCreateBuffers(1, &m_renderer_id);
#endif
#ifdef HN_PLATFORM_MACOS
        glGenBuffers(1, &m_renderer_id);
#endif
        glBindBuffer(GL_ARRAY_BUFFER, m_renderer_id);
        glBufferData(GL_ARRAY_BUFFER, size, vertices, GL_STATIC_DRAW);
    }

    OpenGLVertexBuffer::~OpenGLVertexBuffer() {
        HN_PROFILE_FUNCTION();

        glDeleteBuffers(1, &m_renderer_id);
    }

    void OpenGLVertexBuffer::bind() const {
        HN_PROFILE_FUNCTION();

        glBindBuffer(GL_ARRAY_BUFFER, m_renderer_id);
    }

    void OpenGLVertexBuffer::unbind() const {
        HN_PROFILE_FUNCTION();

        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

	void OpenGLVertexBuffer::set_data(const void *data, uint32_t size) {
		glBindBuffer(GL_ARRAY_BUFFER, m_renderer_id);
    	glBufferSubData(GL_ARRAY_BUFFER, 0, size, data);
	}


    // Index Buffer //////////////////////////////////////////////////////

    OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t *indices, uint32_t count)
        : m_count(count) {
        HN_PROFILE_FUNCTION();

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
        glCreateBuffers(1, &m_renderer_id);
#endif
#ifdef HN_PLATFORM_MACOS
        glGenBuffers(1, &m_renderer_id);
#endif
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_renderer_id);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count * sizeof(uint32_t), indices, GL_STATIC_DRAW);
    }

    OpenGLIndexBuffer::~OpenGLIndexBuffer() {
        HN_PROFILE_FUNCTION();

        glDeleteBuffers(1, &m_renderer_id);
    }

    void OpenGLIndexBuffer::bind() const {
        HN_PROFILE_FUNCTION();

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_renderer_id);
    }

    void OpenGLIndexBuffer::unbind() const {
        HN_PROFILE_FUNCTION();

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    // Uniform Buffer //////////////////////////////////////////////////////

    OpenGLUniformBuffer::OpenGLUniformBuffer(uint32_t size, uint32_t binding)
        : m_size(size), m_binding(binding) {
#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
        glCreateBuffers(1, &m_renderer_id);
#endif
#ifdef HN_PLATFORM_MACOS
        glGenBuffers(1, &m_renderer_id);
#endif
        glBindBuffer(GL_UNIFORM_BUFFER, m_renderer_id);
        glBufferData(GL_UNIFORM_BUFFER, m_size, nullptr, GL_DYNAMIC_DRAW); // reserve space
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // Bind it to a binding point (e.g., slot 0 by default)
        glBindBufferBase(GL_UNIFORM_BUFFER, m_binding, m_renderer_id);
    }

    OpenGLUniformBuffer::~OpenGLUniformBuffer() {
        glDeleteBuffers(1, &m_renderer_id);
    }

    void OpenGLUniformBuffer::bind() const {
        glBindBuffer(GL_UNIFORM_BUFFER, m_renderer_id);
    }

    void OpenGLUniformBuffer::unbind() const {
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void OpenGLUniformBuffer::set_data(uint32_t size, const void *data) {
        glBindBuffer(GL_UNIFORM_BUFFER, m_renderer_id);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, size, data); // update buffer contents
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }


}
