#pragma once

#include "Honey/debug/instrumentor.h"
#include <vulkan/vulkan.h>

namespace Honey::VulkanUtils {

    static uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t type_filter, VkMemoryPropertyFlags props) {
        HN_PROFILE_FUNCTION();
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((type_filter & (1u << i)) && ((mem_props.memoryTypes[i].propertyFlags & props) == props))
                return i;
        }

        HN_CORE_ASSERT(false, "Failed to find suitable Vulkan memory type");
        return 0;
    }

    static VkSamplerCreateInfo make_nearest_sampler_ci() {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;
        si.compareEnable = VK_FALSE;
        si.compareOp     = VK_COMPARE_OP_ALWAYS;
        si.mipLodBias    = 0.0f;
        si.minLod        = 0.0f;
        si.maxLod        = 0.0f;

        si.magFilter  = VK_FILTER_NEAREST;
        si.minFilter  = VK_FILTER_NEAREST;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy    = 1.0f;
        return si;
    }

    static VkSamplerCreateInfo make_linear_sampler_ci() {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;
        si.compareEnable = VK_FALSE;
        si.compareOp     = VK_COMPARE_OP_ALWAYS;
        si.mipLodBias    = 0.0f;
        si.minLod        = 0.0f;
        si.maxLod        = 0.0f;

        si.magFilter  = VK_FILTER_LINEAR;
        si.minFilter  = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy    = 1.0f;
        return si;
    }

    static VkSamplerCreateInfo make_anisotropic_sampler_ci(float max_anisotropy) {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        si.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;
        si.compareEnable = VK_FALSE;
        si.compareOp     = VK_COMPARE_OP_ALWAYS;
        si.mipLodBias    = 0.0f;
        si.minLod        = 0.0f;
        si.maxLod        = 0.0f;

        si.magFilter  = VK_FILTER_LINEAR;
        si.minFilter  = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.anisotropyEnable = VK_TRUE;
        si.maxAnisotropy    = std::max(1.0f, max_anisotropy);
        return si;
    }

    static VkSamplerCreateInfo make_shadow_cmp_sampler_ci() {
        VkSamplerCreateInfo si{};
        si.sType         = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.magFilter     = VK_FILTER_LINEAR;
        si.minFilter     = VK_FILTER_LINEAR;
        si.mipmapMode    = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeV  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.addressModeW  = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        si.borderColor   = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        si.compareEnable = VK_TRUE;
        si.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;

        return si;
    }

    static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    static VkDeviceSize align_down(VkDeviceSize value, VkDeviceSize alignment) {
        return value & ~(alignment - 1);
    }

}