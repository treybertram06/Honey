#pragma once
#include "Honey/renderer/vertex_array.h"

#import <Metal/Metal.h>

#include "Honey/core/core.h"

namespace Honey {

    class MetalVertexArray : public VertexArray {
    public:
        explicit MetalVertexArray(id<MTLDevice> device);

        /* VertexArray overrides ------------------------------------------------*/
        void bind()   const override {}   // global binds don't exist in Metal
        void unbind() const override {}   // — so both are no‑ops

        void add_vertex_buffer(const Ref<VertexBuffer>& vb) override;
        void set_index_buffer(const Ref<IndexBuffer>& ib)   override;

        const std::vector<Ref<VertexBuffer>>& get_vertex_buffers() const override {
            return m_vertex_buffers;
        }
        const Ref<IndexBuffer>& get_index_buffer() const override {
            return m_index_buffer;
        }

        /* Metal‑specific helpers ----------------------------------------------*/
        void bind(id<MTLRenderCommandEncoder> enc) const;    // called by renderer
        MTLVertexDescriptor* vertex_descriptor() const { return m_vertex_desc; }

    private:
        static MTLVertexFormat to_mtl_format(ShaderDataType t);

        id<MTLDevice>                 m_device       = nil;
        MTLVertexDescriptor*          m_vertex_desc  = nil;
        std::vector<Ref<VertexBuffer>>m_vertex_buffers;
        Ref<IndexBuffer>              m_index_buffer;
        std::uint32_t                      m_attr_index   = 0;   // running attrib ID
    };

} // namespace Honey
