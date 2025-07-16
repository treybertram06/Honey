#include "hnpch.h"
#include "opengl_vertex_array.h"

#include <glad/glad.h>

namespace Honey {

    static GLenum shader_data_type_to_opengl_base_type(ShaderDataType type) {

        switch (type) {
            case ShaderDataType::Float:     return GL_FLOAT;
            case ShaderDataType::Float2:    return GL_FLOAT;
            case ShaderDataType::Float3:    return GL_FLOAT;
            case ShaderDataType::Float4:    return GL_FLOAT;
            case ShaderDataType::Mat3:      return GL_FLOAT;
            case ShaderDataType::Mat4:      return GL_FLOAT;
            case ShaderDataType::Int:       return GL_INT;
            case ShaderDataType::Int2:      return GL_INT;
            case ShaderDataType::Int3:      return GL_INT;
            case ShaderDataType::Int4:      return GL_INT;
            case ShaderDataType::Bool:      return GL_BOOL;
            case ShaderDataType::None:      return GL_NONE;
        }
        HN_CORE_ASSERT(false, "Unknown ShaderDataType!");
        return 0;

    }

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
    OpenGLVertexArray::OpenGLVertexArray() {
        HN_PROFILE_FUNCTION();

        glCreateVertexArrays(1, &m_renderer_id);
    }
#endif

#ifdef HN_PLATFORM_MACOS
    OpenGLVertexArray::OpenGLVertexArray() {
        HN_PROFILE_FUNCTION();

        glGenVertexArrays(1, &m_renderer_id);
        glBindVertexArray(m_renderer_id);
    }
#endif

    OpenGLVertexArray::~OpenGLVertexArray() {
        HN_PROFILE_FUNCTION();

        glDeleteVertexArrays(1, &m_renderer_id);
    }


    void OpenGLVertexArray::bind() const {
        HN_PROFILE_FUNCTION();

        glBindVertexArray(m_renderer_id);
    }

    void OpenGLVertexArray::unbind() const {
        HN_PROFILE_FUNCTION();

        glBindVertexArray(0);
    }

    void OpenGLVertexArray::add_vertex_buffer(const Ref<VertexBuffer>& vb)
    {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(vb->get_layout().get_elements().size(), "VertexBuffer has no layout!");

        glBindVertexArray(m_renderer_id);
        vb->bind();

        const auto& layout = vb->get_layout();
        for (const auto& elem : layout)
        {
            glEnableVertexAttribArray(m_attr_index);
            glVertexAttribPointer(m_attr_index,
                                  elem.get_component_count(),
                                  shader_data_type_to_opengl_base_type(elem.type),
                                  elem.normalized ? GL_TRUE : GL_FALSE,
                                  layout.get_stride(),
                                  (const void*)(uintptr_t)elem.offset);

            glVertexAttribDivisor(m_attr_index, elem.instanced ? 1 : 0);
            ++m_attr_index;                                 // advance **once**, persist
        }
        m_vertex_buffers.push_back(vb);
    }

    void OpenGLVertexArray::set_index_buffer(const Ref<IndexBuffer> &index_buffer) {
        HN_PROFILE_FUNCTION();

        glBindVertexArray(m_renderer_id);
        index_buffer->bind();

        m_index_buffer = index_buffer;
    }






}