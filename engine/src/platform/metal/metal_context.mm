#define GLFW_EXPOSE_NATIVE_COCOA     // for glfwGetCocoaWindow
#include <glfw/glfw3.h>
#include <glfw/glfw3native.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include "platform/metal/metal_context.h"

#include "Honey/core/core.h"
#include "Honey/core/log.h"

namespace Honey {

MetalContext::MetalContext(GLFWwindow* window) : m_window(window) {
    HN_CORE_ASSERT(window, "Window handle is null!");
}

void MetalContext::init() {
    // 1) Disable any implicit GL surface GLFW would create.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // 2) Create device & command queue.
    m_device = MTLCreateSystemDefaultDevice();
    HN_CORE_ASSERT(m_device, "No Metal device!");

    m_queue = [m_device newCommandQueue];

    // 3) Attach a CAMetalLayer to the GLFW/Cocoa view.
    NSWindow* nsWin          = glfwGetCocoaWindow(m_window);                                       // :contentReference[oaicite:0]{index=0}
    m_layer                  = [CAMetalLayer layer];
    m_layer.device           = m_device;
    m_layer.pixelFormat      = MTLPixelFormatBGRA8Unorm_sRGB;
    m_layer.framebufferOnly  = YES;
    m_layer.contentsScale    = nsWin.backingScaleFactor;

    nsWin.contentView.wantsLayer = YES;
    nsWin.contentView.layer      = m_layer;

    resize_drawable(); // initial size
    glfwSetWindowSizeCallback(m_window, [](GLFWwindow* window, int width, int height) {
        // Get the context from user pointer or handle resize differently
        // For now, just update the layer size
        NSWindow* nsWin = glfwGetCocoaWindow(window);
        if (nsWin && nsWin.contentView.layer) {
            CAMetalLayer* metalLayer = (CAMetalLayer*)nsWin.contentView.layer;
            metalLayer.drawableSize = CGSizeMake(width, height);
        }
    });

}

void MetalContext::resize_drawable() {
    int w, h;
    glfwGetFramebufferSize(m_window, &w, &h);
    m_layer.drawableSize = CGSizeMake(w, h);
}

void MetalContext::swap_buffers() {
    @autoreleasepool {
        id<CAMetalDrawable> drawable = [m_layer nextDrawable];
        if (!drawable) return;       // Window may be occluded / minimised.

        // Simple “clear‑only” pass.  Replace with your render graph.
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture       = drawable.texture;
        rp.colorAttachments[0].loadAction    = MTLLoadActionClear;
        rp.colorAttachments[0].storeAction   = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor    = MTLClearColorMake(0, 0, 0, 1);

        id<MTLCommandBuffer> cmdBuf  = [m_queue commandBuffer];
        id<MTLRenderCommandEncoder> enc =
            [cmdBuf renderCommandEncoderWithDescriptor:rp];
        [enc endEncoding];

        [cmdBuf presentDrawable:drawable];
        [cmdBuf commit];
    }
}

} // namespace Honey
