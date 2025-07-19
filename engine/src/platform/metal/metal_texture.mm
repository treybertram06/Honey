#include "platform/metal/metal_texture.h"
#include "stb_image.h"
#include "Honey/core/core.h"
#include "Honey/core/log.h"
#include <glfw/glfw3.h>

namespace Honey {

// ---------------------------------------------------------------------------
//  Helpers
// ---------------------------------------------------------------------------
static MTLPixelFormat choose_format(std::uint32_t channels)
{
    // Metal has no 24‑bit RGB; expand to RGBA8.
    return MTLPixelFormatRGBA8Unorm;
}

// ---------------------------------------------------------------------------
//  Empty texture ctor
// ---------------------------------------------------------------------------
MetalTexture2D::MetalTexture2D(id<MTLDevice> dev,
                               std::uint32_t width, std::uint32_t height)
    : m_width(width), m_height(height)
{
    HN_CORE_ASSERT(dev, "Metal device is nil!");
    HN_CORE_ASSERT(width && height, "Dimensions must be non‑zero!");

    auto desc =
         [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                            width:width
                                                           height:height
                                                        mipmapped:NO];     // :contentReference[oaicite:0]{index=0}
    desc.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;

    m_texture = [dev newTextureWithDescriptor:desc];
}

// ---------------------------------------------------------------------------
//  Load‑from‑file ctor
// ---------------------------------------------------------------------------
MetalTexture2D::MetalTexture2D(id<MTLDevice> dev,
                               const std::string& path)
    : m_path(path)
{
    int w, h, chan;
    stbi_set_flip_vertically_on_load(true);
    stbi_uc* data = stbi_load(path.c_str(), &w, &h, &chan, 4); // force RGBA
    HN_CORE_ASSERT(data, "Failed to load image: {}", path);

    m_width  = static_cast<std::uint32_t>(w);
    m_height = static_cast<std::uint32_t>(h);
    chan     = 4;                                    // we forced RGBA

    auto desc =
         [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:
              choose_format(chan)
                                                            width:w
                                                           height:h
                                                        mipmapped:NO];
    desc.usage = MTLTextureUsageShaderRead;

    m_texture = [dev newTextureWithDescriptor:desc];

    // upload pixels
    set_data(data, w * h * chan);

    stbi_image_free(data);
}

// ---------------------------------------------------------------------------
//  Upload / update
// ---------------------------------------------------------------------------
void MetalTexture2D::set_data(void* data, std::uint32_t size)
{
    const std::uint32_t expected = m_width * m_height * 4; // RGBA8 → 4 bytes/px
    HN_CORE_ASSERT(size == expected,
                   "Data size does not match texture dimensions!");

    MTLRegion region = {
        {0, 0, 0},
        {m_width, m_height, 1}
    };
    [m_texture replaceRegion:region           // :contentReference[oaicite:1]{index=1}
                mipmapLevel:0
                  withBytes:data
                bytesPerRow:m_width * 4];
}

// ---------------------------------------------------------------------------
//  Bind helpers
// ---------------------------------------------------------------------------
void MetalTexture2D::bind(std::uint32_t /*slot*/) const
{
    /* No global binding in Metal.
     * Use bind_fragment() just before issuing a draw. */
}

void MetalTexture2D::bind_fragment(id<MTLRenderCommandEncoder> enc,
                                   std::uint32_t slot) const
{
    [enc setFragmentTexture:m_texture atIndex:slot];     // :contentReference[oaicite:2]{index=2}
}

// ---------------------------------------------------------------------------
//  Equality (pointer comparison)
// ---------------------------------------------------------------------------
bool MetalTexture2D::operator==(const Texture& other) const
{
    if (auto* o = dynamic_cast<const MetalTexture2D*>(&other))
        return m_texture == o->m_texture;
    return false;
}

} // namespace Honey
