#pragma once
#include <string>
#include <vulkan/vulkan_core.h>

#include "platform/vulkan/vk_backend.h"
#include "platform/vulkan/vk_types.h"


namespace Honey {

    struct HdrEquirectImage {
        VkImage image;
        VkDeviceMemory image_memory;
        VkImageView view;
        VkSampler sampler;
        uint32_t width, height;
    };

    HdrEquirectImage load_hdr_equirect(const std::string& path,
        VkDevice device, VkPhysicalDevice physical_device, VulkanBackend* backend);

    void destroy_hdr_equirect(HdrEquirectImage& hdr, VkDevice device);

}
