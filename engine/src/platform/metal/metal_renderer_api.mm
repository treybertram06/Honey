#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <Metal/Metal.h>

#include "platform/metal/metal_renderer_api.h"

#include "Honey/core/core.h"
#include "Honey/core/log.h"

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
#define HN_METAL_ASSERT(cond, msg) HN_CORE_ASSERT((cond), msg)
#define HN_METAL_REQUIRE_ENCODER() \
    HN_METAL_ASSERT(m_encoder, "Begin a frame before issuing draw calls!")

namespace Honey {

// ---------------------------------------------------------------------------
//  Ctors / frame boundaries
// ---------------------------------------------------------------------------
MetalRendererAPI::MetalRendererAPI(id<MTLDevice> dev) : m_device(dev) {
    HN_METAL_ASSERT(dev, "Device must not be nil!");
}

void MetalRendererAPI::begin_frame(id<MTLRenderCommandEncoder> enc) {
    m_encoder = enc;
}

void MetalRendererAPI::end_frame() {
    m_encoder = nil;
}

// ---------------------------------------------------------------------------
//  Init & global capability queries
// ---------------------------------------------------------------------------
void MetalRendererAPI::init() {
    // Most fixed‑function toggles in OpenGL map to state baked into
    // a Metal render‑pipeline; nothing to do at run‑time.
}

std::uint32_t MetalRendererAPI::get_max_texture_slots() {
    // Metal exposes the limit via the argument‑buffer sampler count
    // (practically the same limit you care about in a bind‑less engine).
    return static_cast<std::uint32_t>(m_device.maxArgumentBufferSamplerCount);  // :contentReference[oaicite:0]{index=0}
}

// ---------------------------------------------------------------------------
//  Render‑pass helpers
// ---------------------------------------------------------------------------
void MetalRendererAPI::set_clear_color(const glm::vec4& col) { m_clear = col; }

void MetalRendererAPI::clear() {
    // In Metal you clear *via the render‑pass descriptor*, so we simply
    // re‑encode that state immediately.  End the current encoder, create a
    // new one that loads with Clear, then continue issuing commands.
    HN_METAL_REQUIRE_ENCODER();
    id<MTLCommandBuffer> cb = m_encoder.commandBuffer;
    [m_encoder endEncoding];

    MTLRenderPassDescriptor *rp = cb.renderPassDescriptor;
    rp.colorAttachments[0].loadAction  = MTLLoadActionClear;
    rp.colorAttachments[0].clearColor =
        MTLClearColorMake(m_clear.r, m_clear.g, m_clear.b, m_clear.a);

    m_encoder = [cb renderCommandEncoderWithDescriptor:rp];
}

void MetalRendererAPI::set_viewport(std::uint32_t x, std::uint32_t y,
                                    std::uint32_t w, std::uint32_t h) {
    HN_METAL_REQUIRE_ENCODER();

    MTLViewport vp;
    vp.originX = static_cast<double>(x);
    vp.originY = static_cast<double>(y);
    vp.znear   = 0.0;
    vp.zfar    = 1.0;
    vp.width   = static_cast<double>(w);
    vp.height  = static_cast<double>(h);
    [m_encoder setViewport:vp];           // :contentReference[oaicite:1]{index=1}
}

// ---------------------------------------------------------------------------
//  Draw commands
// ---------------------------------------------------------------------------
void MetalRendererAPI::draw_indexed(const Ref<VertexArray>& vao,
                                    std::uint32_t index_count) {
    HN_METAL_REQUIRE_ENCODER();
    vao->bind(m_encoder);                 // assuming your VAO wrapper knows Metal

    const auto& ib = vao->get_index_buffer();
    const std::uint32_t cnt = index_count ? index_count : ib->get_count();

    [m_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                          indexCount:cnt
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:ib->buffer()
                   indexBufferOffset:0]; // :contentReference[oaicite:2]{index=2}
}

void MetalRendererAPI::draw_indexed_instanced(const Ref<VertexArray>& vao,
                                              std::uint32_t index_count,
                                              std::uint32_t instance_count) {
    HN_METAL_REQUIRE_ENCODER();
    vao->bind(m_encoder);

    const auto& ib = vao->get_index_buffer();
    const std::uint32_t cnt = index_count ? index_count : ib->get_count();

    [m_encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                          indexCount:cnt
                           indexType:MTLIndexTypeUInt32
                         indexBuffer:ib->buffer()
                   indexBufferOffset:0
                       instanceCount:instance_count
                           baseVertex:0
                         baseInstance:0];
}

// ---------------------------------------------------------------------------
//  Fixed‑function toggles (wireframe / depth / blending)
// ---------------------------------------------------------------------------
void MetalRendererAPI::set_wireframe(bool mode) {
    HN_METAL_REQUIRE_ENCODER();
    [m_encoder setTriangleFillMode:
        mode ? MTLTriangleFillModeLines : MTLTriangleFillModeFill];        // :contentReference[oaicite:3]{index=3}
}

/*  depth‑test and blending are baked into the MTLRenderPipelineState, so
 *  switching them at run‑time requires swapping pipelines.  Below we simply
 *  assert—they should be routed to your material‑ or pipeline‑cache layer.
 */
void MetalRendererAPI::set_depth_test(bool)  { HN_CORE_ASSERT(false, "Swap pipeline!"); }
void MetalRendererAPI::set_blend(bool)       { HN_CORE_ASSERT(false, "Swap pipeline!"); }

} // namespace Honey
