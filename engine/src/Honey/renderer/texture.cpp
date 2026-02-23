#include "hnpch.h"
#include "texture.h"

#include "texture_cache.h"
#include "Honey/core/task_system.h"
#include "Honey/renderer/renderer.h"
#include "platform/opengl/opengl_texture.h"
#include "platform/vulkan/vk_texture.h"
#include "vendor/tinygltf/stb_image.h"


namespace Honey {
    namespace {
        static bool is_supported_texture_extension(const std::filesystem::path& p) {
            auto ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return (char)std::tolower(c); });

            // Keep this list in sync with what stb_image can actually decode in your build.
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
        }

        static bool texture_file_exists(const std::string& path) {
            namespace fs = std::filesystem;
            std::error_code ec;

            fs::path p(path);

            // Avoid exceptions: these overloads never throw.
            if (!fs::exists(p, ec) || ec)
                return false;

            if (!fs::is_regular_file(p, ec) || ec)
                return false;

            if (!is_supported_texture_extension(p))
                return false;

            return true;
        }
    }

    TextureCache& Texture2D::texture_cache_instance() {
        // Intentionally leaked to avoid static destruction order issues (Vulkan device may be gone).
        static TextureCache* cache = new TextureCache();
        return *cache;
    }

    void Texture2D::shutdown_cache() {
        texture_cache_instance().clear();
    }

    Ref<Texture2D> Texture2D::create(uint32_t width, uint32_t height) {
        switch (Renderer::get_api()) {
        case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
        case RendererAPI::API::opengl:   return CreateRef<OpenGLTexture2D>(width, height);
        case RendererAPI::API::vulkan:   return CreateRef<VulkanTexture2D>(width, height);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;

    }


    Ref<Texture2D> Texture2D::create(const std::string& path) {

        if (texture_cache_instance().contains(path)) {
            return texture_cache_instance().get(path);
        }

        if (!texture_file_exists(path)) {
            HN_CORE_WARN("Texture2D::create: missing/invalid texture path '{}'", path);
            return Texture2D::create("../resources/textures/missing.png");
        }

        switch (Renderer::get_api()) {
        case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
        case RendererAPI::API::opengl:   return texture_cache_instance().add(path, CreateRef<OpenGLTexture2D>(path));
        case RendererAPI::API::vulkan:   return texture_cache_instance().add(path, CreateRef<VulkanTexture2D>(path));
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<Texture2D> Texture2D::create_async(const std::string& path) {
        // 1) If cached, just return it immediately.
        if (texture_cache_instance().contains(path)) {
            return texture_cache_instance().get(path);
        }

        // 2) Validate – if file invalid, just use the normal fallback.
        if (!texture_file_exists(path)) {
            HN_CORE_WARN("Texture2D::create_async: missing/invalid texture path '{}'", path);
            return Texture2D::create("../resources/textures/missing.png");
        }

        // 3) Create small placeholder texture and put it in the cache under 'path'
        const uint32_t placeholderW = 1;
        const uint32_t placeholderH = 1;
        Ref<Texture2D> tex = Texture2D::create(placeholderW, placeholderH);

        // Magenta placeholder
        uint32_t magenta = 0xFF00FFFFu;
        tex->set_data(&magenta, sizeof(magenta));

        tex = texture_cache_instance().add(path, tex);

        // 4) Kick off background load of actual pixels
        TaskSystem::run_async([path, tex]() {
            HN_PROFILE_SCOPE("Texture2D::create_async::stbi_load");

            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
            if (!pixels) {
                HN_CORE_WARN("Texture2D::create_async: stbi_load failed for '{}'", path);
                return; // keep placeholder
            }

            DecodedImageRGBA8 decoded;
            decoded.width  = static_cast<uint32_t>(w);
            decoded.height = static_cast<uint32_t>(h);
            decoded.pixels.resize(decoded.width * decoded.height * 4);
            std::memcpy(decoded.pixels.data(), pixels, decoded.pixels.size());
            stbi_image_free(pixels);

            if (!decoded.ok()) {
                HN_CORE_WARN("Texture2D::create_async: decoded image invalid for '{}'", path);
                return; // keep placeholder
            }

            // GPU upload must happen on main / render thread.
            TaskSystem::enqueue_main([tex, decoded = std::move(decoded)]() mutable {
                HN_PROFILE_SCOPE("Texture2D::create_async::GPU upload");
                if (!tex)
                    return;

                // Resize backend to the real size, then upload pixels.
                tex->resize(decoded.width, decoded.height);
                tex->set_data(decoded.pixels.data(),
                              decoded.width * decoded.height * 4);
            });
        });

        return tex;
    }

    Ref<Texture2D::AsyncHandle> Texture2D::create_async_manual(const std::string& path) {
        auto handle = CreateRef<AsyncHandle>();
        handle->path = path;

        // Already cached: complete immediately.
        if (texture_cache_instance().contains(path)) {
            handle->texture = texture_cache_instance().get(path);
            handle->done.store(true, std::memory_order_release);
            return handle;
        }

        // Validate early; if missing, just use the missing texture synchronously.
        if (!texture_file_exists(path)) {
            HN_CORE_WARN("Texture2D::create_async: missing/invalid texture path '{}'", path);
            handle->texture = Texture2D::create("../resources/textures/missing.png");
            handle->done.store(true, std::memory_order_release);
            return handle;
        }

        // Delegate to backend-specific async implementation.
        switch (Renderer::get_api()) {
        case RendererAPI::API::none:
            HN_CORE_ASSERT(false, "RendererAPI::none is not supported for async textures.");
            handle->failed.store(true, std::memory_order_release);
            handle->error = "RendererAPI::none";
            handle->done.store(true, std::memory_order_release);
            return handle;

        case RendererAPI::API::opengl:
            OpenGLTexture2D::create_async(path, handle);
            break;

        case RendererAPI::API::vulkan:
            VulkanTexture2D::create_async(path, handle);
            break;
        }

        return handle;
    }
}
