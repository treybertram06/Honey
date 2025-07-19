#import "platform/metal/metal_buffer.h"
#include "Honey/core/core.h"
#include "Honey/core/log.h"
#include <glfw/glfw3.h>


namespace Honey {

// ---------------------------------------------------------------------------
//  MetalVertexBuffer
// ---------------------------------------------------------------------------
MetalVertexBuffer::MetalVertexBuffer(id<MTLDevice> dev, std::uint32_t size)
{
    HN_CORE_ASSERT(dev, "Metal device is nil!");

    m_buffer = [dev newBufferWithLength:size
                                options:MTLResourceStorageModeShared];      // :contentReference[oaicite:0]{index=0}
}

MetalVertexBuffer::MetalVertexBuffer(id<MTLDevice> dev,
                                     const float* vertices, std::uint32_t size)
{
    HN_CORE_ASSERT(dev, "Metal device is nil!");

    m_buffer = [dev newBufferWithBytes:vertices
                                length:size
                               options:MTLResourceStorageModeShared];      // :contentReference[oaicite:1]{index=1}
}

void MetalVertexBuffer::set_data(const void* data, std::uint32_t size)
{
    HN_CORE_ASSERT(size <= m_buffer.length,
                   "set_data size exceeds buffer length!");

    std::memcpy(m_buffer.contents, data, size);                            // CPU‑side copy
}

// ---------------------------------------------------------------------------
//  MetalIndexBuffer
// ---------------------------------------------------------------------------
MetalIndexBuffer::MetalIndexBuffer(id<MTLDevice> dev,
                                   const std::uint32_t* indices, std::uint32_t count)
    : m_count(count)
{
    const std::uint32_t byte_len = count * sizeof(std::uint32_t);

    m_buffer = [dev newBufferWithBytes:indices
                                length:byte_len
                               options:MTLResourceStorageModeShared];
}

} // namespace Honey
