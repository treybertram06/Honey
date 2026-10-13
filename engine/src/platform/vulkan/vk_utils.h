#pragma once

#include "Honey/debug/instrumentor.h"
#include <vulkan/vulkan.h>

#include <algorithm>

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
        si.maxLod        = VK_LOD_CLAMP_NONE;

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
        si.maxLod        = VK_LOD_CLAMP_NONE;

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
        si.maxLod        = VK_LOD_CLAMP_NONE;

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

    static VkSamplerCreateInfo make_linear_clamp_sampler_ci() {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        si.unnormalizedCoordinates = VK_FALSE;
        si.compareEnable = VK_FALSE;
        si.compareOp     = VK_COMPARE_OP_ALWAYS;
        si.mipLodBias    = 0.0f;
        si.minLod        = 0.0f;
        si.maxLod        = VK_LOD_CLAMP_NONE;

        si.magFilter  = VK_FILTER_LINEAR;
        si.minFilter  = VK_FILTER_LINEAR;
        si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy    = 1.0f;
        return si;
    }

    static VkDeviceSize align_up(VkDeviceSize value, VkDeviceSize alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    }

    static VkDeviceSize align_down(VkDeviceSize value, VkDeviceSize alignment) {
        return value & ~(alignment - 1);
    }

    // Full mip chain length for a 2D image: floor(log2(max(w, h))) + 1.
    static uint32_t mip_count_for(uint32_t width, uint32_t height) {
        uint32_t levels = 1;
        for (uint32_t dim = std::max(width, height); dim > 1; dim >>= 1)
            ++levels;
        return levels;
    }

    // Pre-copy half of a texture upload: moves every mip of one array layer from old_layout to
    // TRANSFER_DST_OPTIMAL. All mips go together because record_mip_chain leaves them all in one
    // final layout, so on a re-upload they are uniformly SHADER_READ_ONLY (or UNDEFINED when fresh).
    static void record_to_transfer_dst(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout,
                                       uint32_t mip_levels, uint32_t array_layer) {
        if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
            return;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = old_layout;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = mip_levels;
        barrier.subresourceRange.baseArrayLayer = array_layer;
        barrier.subresourceRange.layerCount = 1;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        }

        vkCmdPipelineBarrier(cmd, src_stage, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
    }

    // Post-copy half of a texture upload. Expects mip 0 to hold freshly copied texels and every mip
    // to be in TRANSFER_DST_OPTIMAL (see record_to_transfer_dst). Each level is linearly blitted
    // down from the previous one, then the whole chain ends up in final_layout. With mip_levels == 1
    // this degenerates to the single DST -> final_layout transition the upload paths used to do.
    //
    // vkCmdBlitImage needs a graphics-capable queue (both the immediate and the stream-upload queue
    // are graphics-family). BLIT_SRC/BLIT_DST/SAMPLED_IMAGE_FILTER_LINEAR are mandatory format
    // features for R8G8B8A8_UNORM, so the RGBA8 textures that use this need no format query.
    static void record_mip_chain(VkCommandBuffer cmd, VkImage image, uint32_t width, uint32_t height,
                                 uint32_t mip_levels, uint32_t array_layer, VkImageLayout final_layout) {
        HN_PROFILE_FUNCTION();

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = array_layer;
        barrier.subresourceRange.layerCount = 1;

        int32_t mip_w = static_cast<int32_t>(width);
        int32_t mip_h = static_cast<int32_t>(height);

        for (uint32_t i = 1; i < mip_levels; ++i) {
            // Level i-1 has been written (by the copy or the previous blit): make it the blit source.
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);

            const int32_t next_w = mip_w > 1 ? mip_w / 2 : 1;
            const int32_t next_h = mip_h > 1 ? mip_h / 2 : 1;

            VkImageBlit blit{};
            blit.srcOffsets[1] = { mip_w, mip_h, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = array_layer;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[1] = { next_w, next_h, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = array_layer;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(cmd,
                           image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit, VK_FILTER_LINEAR);

            // Level i-1 is done: hand it to the shaders.
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = final_layout;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                                 0, nullptr, 0, nullptr, 1, &barrier);

            mip_w = next_w;
            mip_h = next_h;
        }

        // The last level was only ever a blit (or copy) destination.
        barrier.subresourceRange.baseMipLevel = mip_levels - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = final_layout;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &barrier);
    }

}