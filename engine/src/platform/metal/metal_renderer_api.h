#pragma once
#include "Honey/renderer/renderer_api.h"

#import <Metal/Metal.h>

namespace Honey {

    /*  A thin wrapper over Metal’s command‑encoder‑style API.
     *  The engine should call `begin_frame()` right after it creates a
     *  `MTLRenderCommandEncoder` for the back‑buffer, then issue the usual
     *  RendererAPI calls.  When the encoder is finished, call `end_frame()`.
     */
    class MetalRendererAPI : public RendererAPI {
    public:
        explicit MetalRendererAPI(id<MTLDevice> device);

        // run‑time hooks ---------------------------------------------------------
        void begin_frame(id<MTLRenderCommandEncoder> encoder);
        void end_frame();                            // sets m_encoder = nil

        // renderer_api overrides -------------------------------------------------
        void init() override;
        void set_clear_color(const glm::vec4& color) override;
        void set_viewport(std::uint32_t x, std::uint32_t y,
                          std::uint32_t width, std::uint32_t height) override;
        void clear() override;

        std::uint32_t get_max_texture_slots() override;

        void draw_indexed(const Ref<VertexArray>& vao,
                          std::uint32_t index_count = 0) override;
        void draw_indexed_instanced(const Ref<VertexArray>& vao,
                                    std::uint32_t index_count,
                                    std::uint32_t instance_count) override;

        void set_wireframe(bool mode) override;
        void set_depth_test(bool mode) override;  // pipeline‑level in Metal
        void set_blend(bool mode) override;       // pipeline‑level in Metal

    private:
        id<MTLDevice>               m_device   = nil;
        id<MTLRenderCommandEncoder> m_encoder  = nil;

        // cached state that influences the next render‑pass creation
        glm::vec4                   m_clear    {0.f, 0.f, 0.f, 1.f};
    };

    /** Convenience helper that instantiates a MetalRendererAPI using the
     *  system default device.  Defined in metal_renderer_api.mm. */
    RendererAPI* create_metal_renderer_api();

} // namespace Honey
