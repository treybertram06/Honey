#import "platform/metal/metal_framebuffer.h"

#include "Honey/core/core.h"
#include "Honey/core/log.h"
#include <glfw/glfw3.h>

namespace Honey {

// ---------------------------------------------------------------------------
//  ctor / dtor
// ---------------------------------------------------------------------------
MetalFramebuffer::MetalFramebuffer(id<MTLDevice> dev,
                                   const FramebufferSpecification& spec)
    : m_device(dev), m_spec(spec)
{
    HN_CORE_ASSERT(dev, "Metal device is nil!");
    invalidate();
}

// ---------------------------------------------------------------------------
//  Private: create / recreate textures & RP descriptor
// ---------------------------------------------------------------------------
void MetalFramebuffer::invalidate()
{
    /* -- Color ----------------------------------------------------------- */
    {
        MTLTextureDescriptor* d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
                 MTLPixelFormatBGRA8Unorm_sRGB
                               width:m_spec.width
                              height:m_spec.height
                           mipmapped:NO];
        d.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;   // :contentReference[oaicite:0]{index=0}
        m_colorTex = [m_device newTextureWithDescriptor:d];
    }

    /* -- Depth/Stencil --------------------------------------------------- */
    {
        MTLTextureDescriptor* d =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
                 MTLPixelFormatDepth32Float_Stencil8                      // 32‑bit depth + 8‑bit stencil
                               width:m_spec.width
                              height:m_spec.height
                           mipmapped:NO];
        d.usage       = MTLTextureUsageRenderTarget;
        d.storageMode = MTLStorageModePrivate;                             // recommended
        m_depthTex    = [m_device newTextureWithDescriptor:d];             // :contentReference[oaicite:1]{index=1}
    }

    /* -- Render‑pass descriptor ----------------------------------------- */
    m_rpDesc = [[MTLRenderPassDescriptor alloc] init];

    auto* ca0 = m_rpDesc.colorAttachments[0];
    ca0.texture       = m_colorTex;
    ca0.loadAction    = MTLLoadActionClear;
    ca0.storeAction   = MTLStoreActionStore;
    ca0.clearColor    = MTLClearColorMake(0, 0, 0, 1);

    m_rpDesc.depthAttachment.texture        = m_depthTex;
    m_rpDesc.depthAttachment.loadAction     = MTLLoadActionClear;
    m_rpDesc.depthAttachment.storeAction    = MTLStoreActionDontCare;
    m_rpDesc.depthAttachment.clearDepth     = 1.0;

    m_rpDesc.stencilAttachment.texture      = m_depthTex;
    m_rpDesc.stencilAttachment.loadAction   = MTLLoadActionClear;
    m_rpDesc.stencilAttachment.storeAction  = MTLStoreActionDontCare;
    m_rpDesc.stencilAttachment.clearStencil = 0;
}

// ---------------------------------------------------------------------------
//  Framebuffer interface
// ---------------------------------------------------------------------------
void MetalFramebuffer::bind()
{
    /* Nothing to “bind” globally.  Typical usage:

           auto enc = [cmdBuf renderCommandEncoderWithDescriptor:
                            framebuffer->render_pass_descriptor()];

       The renderer owns that encoder; the framebuffer just supplies
       the MTLRenderPassDescriptor you see above. */
}

void MetalFramebuffer::resize(std::uint32_t w, std::uint32_t h)
{
    if (w == 0 || h == 0) return;      // skip minimised window etc.

    m_spec.width  = w;
    m_spec.height = h;
    invalidate();
}

} // namespace Honey
