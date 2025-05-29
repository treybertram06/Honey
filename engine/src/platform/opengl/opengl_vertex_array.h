#pragma once

#include "Honey/renderer/vertex_array.h"

namespace Honey {

    class OpenGLVertexArray : public VertexArray {
    public:
        OpenGLVertexArray();
        virtual ~OpenGLVertexArray();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual void add_vertex_buffer(const Ref<VertexBuffer>& vertex_buffer) override;
        virtual void set_index_buffer(const Ref<IndexBuffer>& index_buffer) override;

        virtual const std::vector< Ref<VertexBuffer> >& get_vertex_buffers() const override { return m_vertex_buffers; }
        virtual const Ref<IndexBuffer>& get_index_buffer() const override { return m_index_buffer; }

    private:
        uint32_t m_renderer_id;
        std::vector< Ref<VertexBuffer> > m_vertex_buffers;
        Ref<IndexBuffer> m_index_buffer;
    };

}