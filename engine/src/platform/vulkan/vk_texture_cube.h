#pragma once
#include "vk_texture.h"
#include "Honey/loaders/hdr_equirect_loader.h"

namespace Honey {

    class VulkanTextureCube : public TextureCube {
    public:
        VulkanTextureCube(const std::string& hdr_path);
        ~VulkanTextureCube() override;

        uint32_t get_width() const override { return m_face_size; }
        uint32_t get_bindless_index() const override { return m_bindless_index; }

    private:
        void fetch_device_handles();
        void create_image();
        void create_image_view();
        void create_sampler();
        void update_bindless_descriptor();

        void convert_equirect_to_cube(VulkanBackend* backend, const HdrEquirectImage& src,
            VkImage dst_cube_image, uint32_t face_size, uint32_t mip_levels);

    private:
        uint32_t m_face_size = 0;
        uint32_t m_mip_levels = 0;

        VulkanBackend* m_backend = nullptr;
        void* m_device = nullptr;
        void* m_physical_device = nullptr;

        void* m_image = nullptr;
        void* m_image_memory = nullptr;
        void* m_image_view = nullptr; // Full VK_IMAGE_VIEW_TYPE_CUBE
        VkImageViewCreateInfo m_image_view_ci{};
        void* m_sampler = nullptr;
        uint32_t m_bindless_index = UINT32_MAX;
    };

}
