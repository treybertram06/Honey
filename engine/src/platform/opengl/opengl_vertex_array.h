#pragma once

#include "Honey/renderer/vertex_array.h"

namespace Honey {

    class OpenGLVertexArray : public VertexArray {
    public:
        OpenGLVertexArray();
        virtual ~OpenGLVertexArray();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual void add_vertex_buffer(const std::shared_ptr<VertexBuffer>& vertex_buffer) override;
        virtual void set_index_buffer(const std::shared_ptr<IndexBuffer>& index_buffer) override;

        virtual const std::vector< std::shared_ptr<VertexBuffer> >& get_vertex_buffers() const override { return m_vertex_buffers; }
        virtual const std::shared_ptr<IndexBuffer>& get_index_buffer() const override { return m_index_buffer; }

    private:
        uint32_t m_renderer_id;
        std::vector< std::shared_ptr<VertexBuffer> > m_vertex_buffers;
        std::shared_ptr<IndexBuffer> m_index_buffer;
    };

}