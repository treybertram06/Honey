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

#ifdef HN_PLATFORM_WINDOWS
    OpenGLVertexArray::OpenGLVertexArray() {
        glCreateVertexArrays(1, &m_renderer_id);
    }
#endif

#ifdef HN_PLATFORM_MACOS
    OpenGLVertexArray::OpenGLVertexArray() {
        glGenVertexArrays(1, &m_renderer_id);
        glBindVertexArray(m_renderer_id);
    }
#endif

    OpenGLVertexArray::~OpenGLVertexArray() {
        glDeleteVertexArrays(1, &m_renderer_id);
    }


    void OpenGLVertexArray::bind() const {
        glBindVertexArray(m_renderer_id);
    }

    void OpenGLVertexArray::unbind() const {
        glBindVertexArray(0);
    }

    void OpenGLVertexArray::add_vertex_buffer(const Ref<VertexBuffer> &vertex_buffer) {

        HN_CORE_ASSERT(vertex_buffer->get_layout().get_elements().size(), "VertexBuffer has no layout!");

        glBindVertexArray(m_renderer_id);
        vertex_buffer->bind();

        uint32_t index = 0;
        const auto& layout = vertex_buffer->get_layout();
        for (const auto& element : layout) {
            glEnableVertexAttribArray(index);
            glVertexAttribPointer(index, element.get_component_count(),
                shader_data_type_to_opengl_base_type(element.type),
                element.normalized ? GL_TRUE : GL_FALSE,
                layout.get_stride(),
                (const void*)element.offset);
            index++;
        }

        m_vertex_buffers.push_back(vertex_buffer);
    }

    void OpenGLVertexArray::set_index_buffer(const Ref<IndexBuffer> &index_buffer) {
        glBindVertexArray(m_renderer_id);
        index_buffer->bind();

        m_index_buffer = index_buffer;
    }






}