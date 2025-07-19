#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <cstring>

#include "Honey/core/core.h"
#include "Honey/renderer/buffer.h"

#import <Metal/Metal.h>

namespace Honey {

// ---------------------------------------------------------------------------
//  Vertex Buffer -------------------------------------------------------------
// ---------------------------------------------------------------------------

class MetalVertexBuffer : public VertexBuffer {
public:
    MetalVertexBuffer(id<MTLDevice> device, std::uint32_t size);                 // dynamic
    MetalVertexBuffer(id<MTLDevice> device, const float* vertices,
                      std::uint32_t size);                                       // immutable
    ~MetalVertexBuffer() override = default;                                // ARC

    void bind()   const override {}   // no‑op in Metal
    void unbind() const override {}   // no‑op in Metal

    void set_data(const void* data, std::uint32_t size) override;

    void set_layout(const BufferLayout& layout) override { m_layout = layout; }
    const BufferLayout& get_layout() const override       { return m_layout; }

    /** Metal‑side accessor so VertexArray can bind the buffer. */
    id<MTLBuffer> buffer() const { return m_buffer; }

private:
    id<MTLBuffer>  m_buffer = nil;
    BufferLayout   m_layout;
};

// ---------------------------------------------------------------------------
//  Index Buffer --------------------------------------------------------------
// ---------------------------------------------------------------------------

class MetalIndexBuffer : public IndexBuffer {
public:
    MetalIndexBuffer(id<MTLDevice> device,
                     const std::uint32_t* indices, std::uint32_t count);
    ~MetalIndexBuffer() override = default;

    void bind()   const override {}   // no‑op in Metal
    void unbind() const override {}   // no‑op in Metal

    std::uint32_t get_count() const override { return m_count; }
    id<MTLBuffer> buffer() const        { return m_buffer; }

private:
    id<MTLBuffer>  m_buffer = nil;
    std::uint32_t       m_count  = 0;
};

} // namespace Honey
