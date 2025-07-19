#pragma once
#include "Honey/renderer/framebuffer.h"

#import <Metal/Metal.h>

namespace Honey {

    /* -------------------------------------------------------------------------
       MetalFramebuffer
       --------------------------------------------------------------------- */
    class MetalFramebuffer : public Framebuffer {
    public:
        MetalFramebuffer(id<MTLDevice> device,
                         const FramebufferSpecification& spec);
        ~MetalFramebuffer() = default;                       // ARC

        /* Framebuffer interface ----------------------------------------------*/
        void bind()   override;     // creates + returns a render‑pass descriptor
        void unbind() override {}   // no global FBO state in Metal

        void resize(std::uint32_t w, std::uint32_t h) override;

        std::uint32_t get_color_attachment_renderer_id() const override {
            /* Treat the pointer value as an integer—handy for imgui debug etc. */
            return static_cast<std::uint32_t>(
                reinterpret_cast<uintptr_t>(m_colorTex));
        }

        const FramebufferSpecification& get_specification() const override {
            return m_spec;
        }

        /* Metal helpers ------------------------------------------------------*/
        MTLRenderPassDescriptor* render_pass_descriptor() const {
            return m_rpDesc;
        }
        id<MTLTexture> color_texture() const { return m_colorTex; }
        id<MTLTexture> depth_texture() const { return m_depthTex; }

    private:
        void invalidate();    // (re)create textures & descriptor

        id<MTLDevice>          m_device   = nil;
        id<MTLTexture>         m_colorTex = nil;
        id<MTLTexture>         m_depthTex = nil;
        MTLRenderPassDescriptor*
                               m_rpDesc   = nil;

        FramebufferSpecification m_spec;
    };

} // namespace Honey
