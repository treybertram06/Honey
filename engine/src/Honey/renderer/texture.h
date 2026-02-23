#pragma once

#include "../core/base.h"
#include <string>
#include <imgui.h>

namespace Honey {
    class TextureCache;

    struct DecodedImageRGBA8 {
        std::vector<uint8_t> pixels;
        uint32_t width  = 0;
        uint32_t height = 0;
        std::string error;
        bool ok() const { return error.empty() && !pixels.empty() && width > 0 && height > 0; }
    };

    class Texture {
    public:
        virtual ~Texture() = default;

        virtual uint32_t get_width() const = 0;
        virtual uint32_t get_height() const = 0;
        virtual uint32_t get_renderer_id() const = 0;

        virtual void set_data(void* data, uint32_t size) = 0;

        virtual void bind(uint32_t slot = 0) const = 0;

        virtual bool operator==(const Texture& other) const = 0;

        virtual ImTextureID get_imgui_texture_id() { return 0; }
    };

    class Texture2D : public Texture {
    public:
        static Ref<Texture2D> create(uint32_t width, uint32_t height);
        static Ref<Texture2D> create(const std::string& path);

        static TextureCache& texture_cache_instance();

        static Ref<Texture2D> create_async(const std::string& path);

        struct AsyncHandle {
            std::atomic<bool> done{false};
            std::atomic<bool> failed{false};
            Ref<Texture2D> texture;
            std::string path;
            std::string error;
        };
        static Ref<AsyncHandle> create_async_manual(const std::string& path);

        static void shutdown_cache();

        virtual void resize(uint32_t /* width */, uint32_t /* height */) {}
        virtual void refresh_sampler() {}
    };

}
