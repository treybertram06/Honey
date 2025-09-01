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

    void OpenGLVertexArray::add_vertex_buffer(const Ref<VertexBuffer>& vb) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(vb->get_layout().get_elements().size(), "VertexBuffer has no layout!");

        glBindVertexArray(m_renderer_id);
        vb->bind();

        const auto& layout = vb->get_layout();

        auto is_integer_type = [](ShaderDataType t) {
            switch (t) {
                case ShaderDataType::Int:
                case ShaderDataType::Int2:
                case ShaderDataType::Int3:
                case ShaderDataType::Int4:
                case ShaderDataType::Bool:
                    return true;
                default:
                    return false;
            }
        };

        auto component_count = [](ShaderDataType t) -> GLint {
            switch (t) {
                case ShaderDataType::Float:
                case ShaderDataType::Int:
                case ShaderDataType::Bool:  return 1;
                case ShaderDataType::Float2:
                case ShaderDataType::Int2:  return 2;
                case ShaderDataType::Float3:
                case ShaderDataType::Int3:  return 3;
                case ShaderDataType::Float4:
                case ShaderDataType::Int4:  return 4;
                case ShaderDataType::Mat3:  return 3; // 3 column vectors
                case ShaderDataType::Mat4:  return 4; // 4 column vectors
                default:                    return 0;
            }
        };

        auto base_type = [](ShaderDataType t) -> GLenum {
            switch (t) {
                case ShaderDataType::Float:
                case ShaderDataType::Float2:
                case ShaderDataType::Float3:
                case ShaderDataType::Float4:
                case ShaderDataType::Mat3:
                case ShaderDataType::Mat4:  return GL_FLOAT;
                case ShaderDataType::Int:
                case ShaderDataType::Int2:
                case ShaderDataType::Int3:
                case ShaderDataType::Int4:
                case ShaderDataType::Bool:  return GL_INT;
                default:                    return GL_NONE;
            }
        };

        const GLsizei stride = static_cast<GLsizei>(layout.get_stride());

        for (const auto& elem : layout) {
            const bool instanced = elem.instanced;
            const bool integerPath = is_integer_type(elem.type);
            const GLenum glBase   = base_type(elem.type);

            // Matrices need one attribute per column (OpenGL treats matNxM as N vecM attributes)
            if (elem.type == ShaderDataType::Mat3 || elem.type == ShaderDataType::Mat4) {
                HN_CORE_ASSERT(!integerPath, "Integer matrix attributes are not supported");

                const GLint cols      = component_count(elem.type);          // 3 for mat3, 4 for mat4
                const GLint colComps  = (elem.type == ShaderDataType::Mat3) ? 3 : 4; // vec3 / vec4 per column
                const GLsizei colSize = static_cast<GLsizei>(sizeof(float) * colComps);

                for (GLint c = 0; c < cols; ++c)
                {
                    glEnableVertexAttribArray(m_attr_index + c);
                    glVertexAttribPointer(
                        m_attr_index + c,
                        colComps,
                        GL_FLOAT,
                        elem.normalized ? GL_TRUE : GL_FALSE,
                        stride,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(elem.offset + c * colSize))
                    );
                    if (instanced) glVertexAttribDivisor(m_attr_index + c, 1);
                }
                m_attr_index += cols; // consumed multiple locations
            }
            else
            {
                glEnableVertexAttribArray(m_attr_index);

                if (integerPath)
                {
                    // Pure integer attributes (ivec*, uvec*, bool)
                    glVertexAttribIPointer(
                        m_attr_index,
                        component_count(elem.type),
                        glBase,                     // GL_INT or GL_UNSIGNED_INT
                        stride,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(elem.offset))
                    );
                } else {
                    // Float / normalized types
                    glVertexAttribPointer(
                        m_attr_index,
                        component_count(elem.type),
                        glBase,                     // GL_FLOAT
                        elem.normalized ? GL_TRUE : GL_FALSE,
                        stride,
                        reinterpret_cast<const void*>(static_cast<uintptr_t>(elem.offset))
                    );
                }

                glVertexAttribDivisor(m_attr_index, instanced ? 1 : 0);
                ++m_attr_index; // advance **once** per scalar/vector attribute
            }
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