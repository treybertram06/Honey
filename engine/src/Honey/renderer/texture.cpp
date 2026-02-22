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

    Ref<Texture2D::AsyncHandle> Texture2D::create_async(const std::string& path) {
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
            HN_CORE_TRACE("Texture2D::create_async: Vulkan texture async creation starting.");
            VulkanTexture2D::create_async(path, handle);
            break;
        }

        return handle;
    }
}
