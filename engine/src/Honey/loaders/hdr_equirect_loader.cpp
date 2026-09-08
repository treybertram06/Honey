#include "hnpch.h"
#include "hdr_equirect_loader.h"

#include <complex>

#include "Honey/core/settings.h"
#include "platform/vulkan/vk_utils.h"
#include "vendor/tinygltf/stb_image.h"

namespace Honey {
    
    HdrEquirectImage load_hdr_equirect(const std::string& path,
        VkDevice device, VkPhysicalDevice physical_device, VulkanBackend* backend) {
        HdrEquirectImage hdr = {};
        
        int width, height, channels;
        float* pixels = stbi_loadf(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        HN_CORE_ASSERT(pixels, "Failed to load HDR image: {0}", path);
        HN_CORE_ASSERT(width > 0 && height > 0, "HDR image must have positive dimensions");

        hdr.width = width;
        hdr.height = height;
        uint32_t size = width * height * 4 * sizeof(float);
        
        // Create VkImage 
        VkImageCreateInfo image_ci = {};
        image_ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image_ci.imageType = VK_IMAGE_TYPE_2D;
        image_ci.extent.width = width;
        image_ci.extent.height = height;
        image_ci.extent.depth = 1;
        image_ci.mipLevels = 1;
        image_ci.arrayLayers = 1;
        image_ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        image_ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
        image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;   
        
        VkImage image_handle = VK_NULL_HANDLE;
        VkResult r = vkCreateImage(device, &image_ci, nullptr, &image_handle);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImage failed");
        hdr.image = image_handle;
        
        // Create VkImageMemory
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, image_handle, &req);
        
        VkMemoryAllocateInfo ai = {};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(physical_device, 
            req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        
        VkDeviceMemory mem_handle = VK_NULL_HANDLE;
        r = vkAllocateMemory(device, &ai, nullptr, &mem_handle);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory failed");
        
        r = vkBindImageMemory(device, image_handle, mem_handle, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindImageMemory failed");
        
        hdr.image_memory = mem_handle;
        
        // Create VkImageView
        VkImageViewCreateInfo view_ci{};
        view_ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view_ci.image = hdr.image;
        view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view_ci.format = VK_FORMAT_R32G32B32A32_SFLOAT;
        view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view_ci.subresourceRange.baseMipLevel = 0;
        view_ci.subresourceRange.levelCount = 1;
        view_ci.subresourceRange.baseArrayLayer = 0;
        view_ci.subresourceRange.layerCount = 1;

        VkImageView image_view = VK_NULL_HANDLE;
        r = vkCreateImageView(device, &view_ci, nullptr, &image_view);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImageView failed");
        hdr.view = image_view;
        
        // Create sampler
        VkSamplerCreateInfo sampler_ci{};
        sampler_ci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        sampler_ci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_ci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        sampler_ci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_ci.borderColor  = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
        sampler_ci.unnormalizedCoordinates = VK_FALSE;
        sampler_ci.compareEnable = VK_FALSE;
        sampler_ci.compareOp     = VK_COMPARE_OP_ALWAYS;
        sampler_ci.mipLodBias    = 0.0f;
        sampler_ci.minLod        = 0.0f;
        sampler_ci.maxLod        = 0.0f;
        sampler_ci.anisotropyEnable = VK_FALSE;
        sampler_ci.magFilter  = VK_FILTER_LINEAR;
        sampler_ci.minFilter  = VK_FILTER_LINEAR;
        sampler_ci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        
        VkSampler sampler = VK_NULL_HANDLE;
        r = vkCreateSampler(device, &sampler_ci, nullptr, &sampler);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateSampler failed");
        hdr.sampler = sampler;
        
        // Create staging buffer
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer staging_buffer = VK_NULL_HANDLE;
        r = vkCreateBuffer(device, &bi, nullptr, &staging_buffer);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer (staging) failed");

        req = VkMemoryRequirements{};
        vkGetBufferMemoryRequirements(device, staging_buffer, &req);

        ai = VkMemoryAllocateInfo{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(physical_device, req.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDeviceMemory staging_memory = VK_NULL_HANDLE;
        r = vkAllocateMemory(device, &ai, nullptr, &staging_memory);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory (staging) failed");

        r = vkBindBufferMemory(device, staging_buffer, staging_memory, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory (staging) failed");
        
        // Copy data to staging buffer
        void* mapped = nullptr;
        r = vkMapMemory(device, staging_memory, 0, size, 0, &mapped);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory failed for staging buffer");
        std::memcpy(mapped, pixels, size);
        vkUnmapMemory(device, staging_memory);
        
        // Free the image memory
        stbi_image_free(pixels);
        
        backend->immediate_submit([&](VkCommandBuffer cmd) {
            // Transition image layout
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = hdr.image;
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            vkCmdPipelineBarrier(cmd,
                                 src_stage, dst_stage,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);

            // Copy buffer to image
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = { static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1 };

            vkCmdCopyBufferToImage(cmd,
                                   staging_buffer,
                                   hdr.image,
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &region);

            // Transition image layout to SHADER_READ_ONLY_OPTIMAL, reusing old barrier
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

            vkCmdPipelineBarrier(cmd,
                     src_stage, dst_stage,
                     0,
                     0, nullptr,
                     0, nullptr,
                     1, &barrier);

        });

        vkDestroyBuffer(device, staging_buffer, nullptr);
        vkFreeMemory(device, staging_memory, nullptr);

        return hdr;
    }

    void destroy_hdr_equirect(HdrEquirectImage& hdr, VkDevice device) {
        vkDestroyImageView(device, hdr.view, nullptr);
        vkDestroySampler(device, hdr.sampler, nullptr);
        vkDestroyImage(device, hdr.image, nullptr);
        vkFreeMemory(device, hdr.image_memory, nullptr);

        hdr.view = VK_NULL_HANDLE;
        hdr.sampler = VK_NULL_HANDLE;
        hdr.image = VK_NULL_HANDLE;
        hdr.image_memory = VK_NULL_HANDLE;
        hdr.width = 0;
        hdr.height = 0;
    }
    
}
