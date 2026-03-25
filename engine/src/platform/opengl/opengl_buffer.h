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
        OpenGLIndexBuffer(uint16_t* indices, uint32_t count);
        virtual ~OpenGLIndexBuffer();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual uint32_t get_count() const override { return m_count; }

    private:
        uint32_t m_renderer_id;
        uint32_t m_count;
    };

    class OpenGLUniformBuffer : public UniformBuffer {
    public:
        OpenGLUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~OpenGLUniformBuffer();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual void set_data(uint32_t size, const void* data) override;

    private:
        uint32_t m_renderer_id;
        uint32_t m_size;
        uint32_t m_binding;
    };

    class OpenGLStorageBuffer : public StorageBuffer {
    public:
        OpenGLStorageBuffer(uint32_t size, uint32_t usage_flags = 0);
        ~OpenGLStorageBuffer() override;

        void bind(uint32_t binding = 0) const override;
        void unbind() const override;

        void set_data(const void* data, uint32_t size, uint32_t offset = 0) override;
        uint32_t get_size() const override { return m_size; }

        void* get_native_buffer() const override;

    private:
        uint32_t m_renderer_id = 0;
        uint32_t m_size = 0;
    };
}