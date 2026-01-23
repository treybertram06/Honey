#pragma once

#include "Honey/renderer/texture.h"

#include <string>
#include <cstdint>

namespace Honey {

    class VulkanBackend;

    class VulkanTexture2D : public Texture2D {
    public:
        VulkanTexture2D(uint32_t width, uint32_t height);
        VulkanTexture2D(const std::string& path);
        ~VulkanTexture2D() override;

        uint32_t get_width() const override { return m_width; }
        uint32_t get_height() const override { return m_height; }

        uint32_t get_renderer_id() const override { return 0; }

        void set_data(void* data, uint32_t size) override;
        void bind(uint32_t /*slot*/) const override {} // Vulkan uses descriptor sets

        bool operator==(const Texture& other) const override;

        void* get_vk_image_view() const { return m_image_view; }
        void* get_vk_sampler() const { return m_sampler; }
        uint32_t get_vk_image_layout() const { return m_current_layout; }

    private:
        void fetch_device_handles();
        void init_from_pixels_rgba8(const void* rgba_pixels, uint32_t width, uint32_t height);

        void create_image(uint32_t width, uint32_t height);
        void create_image_view();
        void create_sampler();

        void transition_image_layout(uint32_t old_layout, uint32_t new_layout);
        void copy_buffer_to_image(void* staging_buffer);

        void create_staging_buffer(uint32_t size_bytes, void*& out_buffer, void*& out_memory);
        void destroy_buffer(void*& buffer, void*& memory);

        uint32_t find_memory_type(uint32_t type_filter, uint32_t props);

    private:
        uint32_t m_width = 0;
        uint32_t m_height = 0;
        std::string m_path;

        VulkanBackend* m_backend = nullptr;

        void* m_device = nullptr;
        void* m_physical_device = nullptr;

        void* m_image = nullptr;
        void* m_image_memory = nullptr;
        void* m_image_view = nullptr;
        void* m_sampler = nullptr;

        uint32_t m_current_layout = 0;
    };

} // namespace Honey