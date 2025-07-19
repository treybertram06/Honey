#import "platform/metal/metal_vertex_array.h"
#import "platform/metal/metal_buffer.h"   // for buffer() accessors
#include "Honey/core/core.h"
#include "Honey/core/log.h"
#include <glfw/glfw3.h>

namespace Honey {

// ---------------------------------------------------------------------------
//  Helper: map ShaderDataType → MTLVertexFormat
// ---------------------------------------------------------------------------
MTLVertexFormat MetalVertexArray::to_mtl_format(ShaderDataType t) {
    switch (t) {
        case ShaderDataType::Float:   return MTLVertexFormatFloat;     // :contentReference[oaicite:0]{index=0}
        case ShaderDataType::Float2:  return MTLVertexFormatFloat2;
        case ShaderDataType::Float3:  return MTLVertexFormatFloat3;
        case ShaderDataType::Float4:  return MTLVertexFormatFloat4;
        case ShaderDataType::Int:     return MTLVertexFormatInt;
        case ShaderDataType::Int2:    return MTLVertexFormatInt2;
        case ShaderDataType::Int3:    return MTLVertexFormatInt3;
        case ShaderDataType::Int4:    return MTLVertexFormatInt4;
        case ShaderDataType::Bool:    return MTLVertexFormatUChar;
        default:                      return MTLVertexFormatInvalid;
    }
}

// ---------------------------------------------------------------------------
//  Ctor / dtor
// ---------------------------------------------------------------------------
MetalVertexArray::MetalVertexArray(id<MTLDevice> dev) : m_device(dev) {
    HN_CORE_ASSERT(dev, "Metal device is nil!");
    m_vertex_desc = [MTLVertexDescriptor vertexDescriptor];
}

// ---------------------------------------------------------------------------
//  Add vertex & index buffers
// ---------------------------------------------------------------------------
void MetalVertexArray::add_vertex_buffer(const Ref<VertexBuffer>& vb) {
    HN_CORE_ASSERT(vb->get_layout().get_elements().size(),
                   "VertexBuffer has no layout!");

    const std::uint32_t buffer_index = static_cast<std::uint32_t>(m_vertex_buffers.size());
    const auto&    layout       = vb->get_layout();

    // Record attribute metadata in the vertex descriptor
    for (const auto& elem : layout) {
        auto* attr                     = m_vertex_desc->attributes[m_attr_index];
        attr.format                    = to_mtl_format(elem.type);
        attr.offset                    = elem.offset;
        attr.bufferIndex               = buffer_index;

        auto* bufLayout                = m_vertex_desc->layouts[buffer_index];
        bufLayout.stride               = layout.get_stride();
        bufLayout.stepRate             = 1;
        bufLayout.stepFunction         =
            elem.instanced ? MTLVertexStepFunctionPerInstance
                           : MTLVertexStepFunctionPerVertex;

        ++m_attr_index;
    }

    m_vertex_buffers.push_back(vb);
}

void MetalVertexArray::set_index_buffer(const Ref<IndexBuffer>& ib) {
    m_index_buffer = ib;
}

// ---------------------------------------------------------------------------
//  Bind to a command encoder just before drawing
// ---------------------------------------------------------------------------
void MetalVertexArray::bind(id<MTLRenderCommandEncoder> enc) const {
    // Vertex buffers
    for (std::uint32_t i = 0; i < m_vertex_buffers.size(); ++i) {
        auto metalVB =
            static_cast<const MetalVertexBuffer*>(m_vertex_buffers[i].get());
        [enc setVertexBuffer:metalVB->buffer() offset:0 atIndex:i];      // :contentReference[oaicite:1]{index=1}
    }

    // Index buffer (slot 0 by convention)
    if (m_index_buffer) {
        auto metalIB =
            static_cast<const MetalIndexBuffer*>(m_index_buffer.get());
        [enc setIndexBuffer:metalIB->buffer() offset:0
                 indexType:MTLIndexTypeUInt32];
    }
}

} // namespace Honey
