#include "hnpch.h"
#include "opengl_buffer.h"

#include <glad/glad.h>

namespace Honey {

    // Vertex Buffer //////////////////////////////////////////////////////


    OpenGLVertexBuffer::OpenGLVertexBuffer(float* vertices, uint32_t size) {
        HN_PROFILE_FUNCTION();

#ifdef HN_PLATFORM_WINDOWS
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

    // Index Buffer //////////////////////////////////////////////////////

    OpenGLIndexBuffer::OpenGLIndexBuffer(uint32_t *indices, uint32_t count)
        : m_count(count) {
        HN_PROFILE_FUNCTION();

#ifdef HN_PLATFORM_WINDOWS
        glCreateBuffers(1, &m_renderer_id);
#endif
#ifdef HN_PLATFORM_MACOS
        glGenBuffers(1, &m_renderer_id);
#endif
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_renderer_id);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, count*sizeof(uint32_t), indices, GL_STATIC_DRAW);
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




}