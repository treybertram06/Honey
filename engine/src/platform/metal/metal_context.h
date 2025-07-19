#pragma once
#include <MacTypes.h>
#include <objc/objc.h>

#include "Honey/renderer/graphics_context.h"

@class CAMetalLayer;
@protocol MTLDevice;
@protocol MTLCommandQueue;

struct GLFWwindow;

namespace Honey {

    class MetalContext : public GraphicsContext {
    public:
        explicit MetalContext(GLFWwindow* window);

        void init() override;
        void swap_buffers() override;

        id<MTLDevice> device() const { return m_device; }

    private:
        void resize_drawable();

        GLFWwindow*        m_window = nullptr;
        id<MTLDevice>      m_device = nil;
        id<MTLCommandQueue>m_queue  = nil;
        CAMetalLayer*      m_layer  = nil;
    };

} // namespace Honey