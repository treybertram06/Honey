#pragma once

#include "Honey/renderer/buffer.h"

namespace Honey {

    class OpenGLVertexBuffer : public VertexBuffer {
    public:
        OpenGLVertexBuffer(uint32_t size);
        OpenGLVertexBuffer(float* vertices, uint32_t size);
        virtual ~OpenGLVertexBuffer();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual void set_data(const void *data, uint32_t size) override;

        virtual void set_layout(const BufferLayout& layout) override { m_layout = layout; }
        virtual const BufferLayout& get_layout() const override { return m_layout; }

    private:
        uint32_t m_renderer_id;
        BufferLayout m_layout;
    };

    class OpenGLIndexBuffer : public IndexBuffer {
    public:
        OpenGLIndexBuffer(uint32_t* indices, uint32_t count);
        virtual ~OpenGLIndexBuffer();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual uint32_t get_count() const override { return m_count; }

    private:
        uint32_t m_renderer_id;
        uint32_t m_count;
    };
}