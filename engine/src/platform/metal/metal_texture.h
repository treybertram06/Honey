#pragma once
#include "Honey/renderer/texture.h"

#import <Metal/Metal.h>

namespace Honey {

    class MetalTexture2D : public Texture2D {
    public:
        /** Create an **empty** RGBA8 texture you can later fill with `set_data`. */
        MetalTexture2D(id<MTLDevice> device,
                       std::uint32_t width, std::uint32_t height);

        /** Load from disk via stb_image (flipped vertically, always 4 channels). */
        MetalTexture2D(id<MTLDevice> device,
                       const std::string& path);

        ~MetalTexture2D() override = default;        // ARC handles release

        // Texture2D interface ---------------------------------------------------
        std::uint32_t get_width()  const override { return m_width;  }
        std::uint32_t get_height() const override { return m_height; }
        std::uint32_t get_renderer_id() const override { return 0; } // not meaningful

        void  set_data(void* data, std::uint32_t size) override;
        void  bind(std::uint32_t slot = 0) const override;           // no‑op in Metal

        bool operator==(const Texture& other) const override;

        // Metal‑side accessor ---------------------------------------------------
        id<MTLTexture> texture() const { return m_texture; }

        /** Convenience: bind to fragment stage. */
        void bind_fragment(id<MTLRenderCommandEncoder> enc, std::uint32_t slot) const;

    private:
        id<MTLTexture> m_texture = nil;
        std::uint32_t       m_width   = 0;
        std::uint32_t       m_height  = 0;
        std::string    m_path;
    };

} // namespace Honey
