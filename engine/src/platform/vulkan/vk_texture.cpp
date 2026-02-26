#include "hnpch.h"
#include "vk_texture.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "Honey/core/engine.h"
#include "platform/vulkan/vk_backend.h"

#include "stb_image.h"

#include <cstring>
#include <vector>

#include "imgui_impl_vulkan.h"
#include "Honey/core/settings.h"
#include "Honey/core/task_system.h"
#include "Honey/renderer/texture_cache.h"

namespace Honey {
    VulkanTexture2D::VulkanTexture2D(uint32_t width, uint32_t height)
        : m_width(width), m_height(height) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(width > 0 && height > 0, "VulkanTexture2D: invalid size");
        fetch_device_handles();

        std::vector<uint8_t> empty(m_width * m_height * 4, 0);

        // For tiny placeholder textures, avoid the streaming path and do a
        // synchronous upload so they are immediately sampleable.
        if (m_width == 1 && m_height == 1) {
            // Explicitly create resources and do an immediate upload.
            create_image(m_width, m_height);
            create_image_view();
            create_sampler();

            // Use the old immediate path: transition -> copy -> transition.
            const uint32_t size = m_width * m_height * 4;

            void* staging_buffer = nullptr;
            void* staging_memory = nullptr;
            create_staging_buffer(size, staging_buffer, staging_memory);

            VkDevice dev = reinterpret_cast<VkDevice>(m_device);
            void* mapped = nullptr;
            VkResult r = vkMapMemory(dev,
                                     reinterpret_cast<VkDeviceMemory>(staging_memory),
                                     0, size, 0, &mapped);
            HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory failed for placeholder staging buffer");
            std::memcpy(mapped, empty.data(), size);
            vkUnmapMemory(dev, reinterpret_cast<VkDeviceMemory>(staging_memory));

            transition_image_layout(VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            copy_buffer_to_image(staging_buffer);
            transition_image_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

            destroy_buffer(staging_buffer, staging_memory);
        } else {
            // Normal textures use the streaming‑friendly initialization.
            init_from_pixels_rgba8(empty.data(), m_width, m_height);
        }
    }

    VulkanTexture2D::VulkanTexture2D(const std::string& path)
        : m_path(path) {
        HN_PROFILE_FUNCTION();
        fetch_device_handles();

        int w = 0, h = 0, channels = 0;
        stbi_uc* pixels;
        {
            std::string profiler_title = "VulkanTexture2D: stbi_load - " + path;
            HN_PROFILE_SCOPE(profiler_title.c_str());

            pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
            HN_CORE_ASSERT(pixels, "Failed to load image: {0}", path);
        }
        m_width = static_cast<uint32_t>(w);
        m_height = static_cast<uint32_t>(h);

        init_from_pixels_rgba8(pixels, m_width, m_height);
        stbi_image_free(pixels);
    }

    VulkanTexture2D::~VulkanTexture2D() {
        HN_PROFILE_FUNCTION();
        if (!m_backend || !m_backend->initialized() || !m_device)
            return;

        VkDevice dev = reinterpret_cast<VkDevice>(m_device);
        HN_CORE_ASSERT(dev == m_backend->device(),
                           "VulkanTexture2D: device mismatch in destructor (backend was probably shut down earlier)");

        if (m_imgui_texture_id != 0 && m_backend->imgui_initialized()) {
            ImGui_ImplVulkan_RemoveTexture(
                reinterpret_cast<VkDescriptorSet>(m_imgui_texture_id)
            );
            m_imgui_texture_id = 0;
        }

        if (m_sampler) {
            vkDestroySampler(dev, reinterpret_cast<VkSampler>(m_sampler), nullptr);
            m_sampler = nullptr;
        }
        if (m_image_view) {
            vkDestroyImageView(dev, reinterpret_cast<VkImageView>(m_image_view), nullptr);
            m_image_view = nullptr;
        }
        if (m_image) {
            vkDestroyImage(dev, reinterpret_cast<VkImage>(m_image), nullptr);
            m_image = nullptr;
        }
        if (m_image_memory) {
            vkFreeMemory(dev, reinterpret_cast<VkDeviceMemory>(m_image_memory), nullptr);
            m_image_memory = nullptr;
        }

        m_backend = nullptr;
        m_device = nullptr;
        m_physical_device = nullptr;
    }

    void VulkanTexture2D::create_async(const std::string& path, const Ref<AsyncHandle>& handle) {
        // CPU-side load on worker thread
        TaskSystem::run_async([path, handle]() {
            HN_PROFILE_SCOPE("VulkanTexture2D::create_async::stbi_load");

            int w = 0, h = 0, channels = 0;
            stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);

            if (!pixels) {
                handle->failed.store(true, std::memory_order_release);
                handle->error = "stbi_load failed";
                HN_CORE_WARN("VulkanTexture2D::create_async: stbi_load failed for '{}'", path);
                handle->done.store(true, std::memory_order_release);
                return;
            }

            DecodedImageRGBA8 decoded;
            decoded.width  = static_cast<uint32_t>(w);
            decoded.height = static_cast<uint32_t>(h);
            decoded.pixels.resize(decoded.width * decoded.height * 4);
            std::memcpy(decoded.pixels.data(), pixels, decoded.pixels.size());
            stbi_image_free(pixels);

            // GPU work must run on main thread while VulkanBackend is alive.
            TaskSystem::enqueue_main([path, handle, decoded = std::move(decoded)]() mutable {
                HN_PROFILE_SCOPE("VulkanTexture2D::create_async::GPU work");
                if (!decoded.ok()) {
                    handle->failed.store(true, std::memory_order_release);
                    handle->error = "decoded image invalid";
                    handle->done.store(true, std::memory_order_release);
                    return;
                }

                // Create Vulkan texture (device handles are accessed via Application::get()).
                Ref<VulkanTexture2D> vk_tex = CreateRef<VulkanTexture2D>(decoded.width, decoded.height);
                vk_tex->m_path = path; // keep original path for logging/samplers
                vk_tex->set_data_streaming(decoded.pixels.data(), static_cast<uint32_t>(decoded.pixels.size()));

                Ref<Texture2D> as_tex = vk_tex;
                as_tex = Texture2D::texture_cache_instance().add(path, as_tex);

                handle->texture = as_tex;
                handle->done.store(true, std::memory_order_release);
            });
        });
    }

    bool VulkanTexture2D::operator==(const Texture& other) const {
        auto* o = dynamic_cast<const VulkanTexture2D*>(&other);
        if (!o) return false;
        return m_image == o->m_image && m_image_view == o->m_image_view;
    }

    void VulkanTexture2D::set_data(const void* data, uint32_t size) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(data, "VulkanTexture2D::set_data - data is null");
        HN_CORE_ASSERT(m_width > 0 && m_height > 0, "VulkanTexture2D::set_data - invalid size");
        HN_CORE_ASSERT(size == m_width * m_height * 4, "VulkanTexture2D::set_data - size mismatch (expected RGBA8)");

        void* staging_buffer = nullptr;
        void* staging_memory = nullptr;
        create_staging_buffer(size, staging_buffer, staging_memory);

        VkDevice dev = reinterpret_cast<VkDevice>(m_device);
        void* mapped = nullptr;
        VkResult r = vkMapMemory(dev, reinterpret_cast<VkDeviceMemory>(staging_memory), 0, size, 0, &mapped);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory failed for staging buffer");
        std::memcpy(mapped, data, size);
        vkUnmapMemory(dev, reinterpret_cast<VkDeviceMemory>(staging_memory));

        transition_image_layout(m_current_layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(staging_buffer);
        transition_image_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        destroy_buffer(staging_buffer, staging_memory);
    }

    void VulkanTexture2D::set_data_streaming(const void* data, uint32_t size) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(data, "VulkanTexture2D::set_data_streaming - data is null");
        HN_CORE_ASSERT(m_width > 0 && m_height > 0, "VulkanTexture2D::set_data_streaming - invalid size");
        HN_CORE_ASSERT(size == m_width * m_height * 4,
                       "VulkanTexture2D::set_data_streaming - size mismatch (expected RGBA8)");

        HN_CORE_ASSERT(m_backend && m_backend->initialized(),
                       "VulkanTexture2D::set_data_streaming: backend not initialized");

        VulkanBackend::ImageUploadDesc desc{};
        desc.dstImage       = reinterpret_cast<VkImage>(m_image);
        desc.width          = m_width;
        desc.height         = m_height;
        desc.mipLevel       = 0;
        desc.arrayLayer     = 0;
        desc.initialLayout  = static_cast<VkImageLayout>(m_current_layout); // usually UNDEFINED for freshly created images
        desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        desc.srcData        = data;
        desc.size           = size;

        m_backend->queue_image_upload(desc);

        // We *expect* the backend to transition to finalLayout in its batched upload path.
        // Keep our cached layout in sync.
        m_current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    void VulkanTexture2D::fetch_device_handles() {
        HN_PROFILE_FUNCTION();
        m_backend = &Application::get().get_vulkan_backend();
        HN_CORE_ASSERT(m_backend && m_backend->initialized(), "VulkanTexture2D: VulkanBackend not initialized");

        VkDevice dev = m_backend->device();
        VkPhysicalDevice phys = m_backend->physical_device();

        HN_CORE_ASSERT(dev && phys, "VulkanTexture2D: Vulkan device not created yet (need a Vulkan window/surface first)");

        m_device = reinterpret_cast<void*>(dev);
        m_physical_device = reinterpret_cast<void*>(phys);
    }

    uint32_t VulkanTexture2D::find_memory_type(uint32_t type_filter, uint32_t props) {
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(reinterpret_cast<VkPhysicalDevice>(m_physical_device), &mem_props);

        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((type_filter & (1u << i)) && ((mem_props.memoryTypes[i].propertyFlags & props) == props))
                return i;
        }

        HN_CORE_ASSERT(false, "VulkanTexture2D: failed to find suitable memory type");
        return 0;
    }

    void VulkanTexture2D::create_staging_buffer(uint32_t size_bytes, void*& out_buffer, void*& out_memory) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size_bytes;
        bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer buf = VK_NULL_HANDLE;
        VkResult r = vkCreateBuffer(reinterpret_cast<VkDevice>(m_device), &bi, nullptr, &buf);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer (staging) failed");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(reinterpret_cast<VkDevice>(m_device), buf, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDeviceMemory mem = VK_NULL_HANDLE;
        r = vkAllocateMemory(reinterpret_cast<VkDevice>(m_device), &ai, nullptr, &mem);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory (staging) failed");

        r = vkBindBufferMemory(reinterpret_cast<VkDevice>(m_device), buf, mem, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory (staging) failed");

        out_buffer = reinterpret_cast<void*>(buf);
        out_memory = reinterpret_cast<void*>(mem);
    }

    void VulkanTexture2D::destroy_buffer(void*& buffer, void*& memory) {
        if (buffer) {
            vkDestroyBuffer(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkBuffer>(buffer), nullptr);
            buffer = nullptr;
        }
        if (memory) {
            vkFreeMemory(reinterpret_cast<VkDevice>(m_device), reinterpret_cast<VkDeviceMemory>(memory), nullptr);
            memory = nullptr;
        }
    }

    void VulkanTexture2D::create_image(uint32_t width, uint32_t height) {
        VkImageCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType = VK_IMAGE_TYPE_2D;
        ci.extent.width = width;
        ci.extent.height = height;
        ci.extent.depth = 1;
        ci.mipLevels = 1;
        ci.arrayLayers = 1;
        ci.format = VK_FORMAT_R8G8B8A8_UNORM;
        ci.tiling = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        ci.samples = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkImage img = VK_NULL_HANDLE;
        VkResult r = vkCreateImage(reinterpret_cast<VkDevice>(m_device), &ci, nullptr, &img);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImage failed");

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(reinterpret_cast<VkDevice>(m_device), img, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_memory_type(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VkDeviceMemory mem = VK_NULL_HANDLE;
        r = vkAllocateMemory(reinterpret_cast<VkDevice>(m_device), &ai, nullptr, &mem);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory (image) failed");

        r = vkBindImageMemory(reinterpret_cast<VkDevice>(m_device), img, mem, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindImageMemory failed");

        m_image = reinterpret_cast<void*>(img);
        m_image_memory = reinterpret_cast<void*>(mem);
        m_current_layout = VK_IMAGE_LAYOUT_UNDEFINED;

        HN_CORE_ASSERT(m_image != nullptr, "create_image: m_image is null after vkCreateImage");
    }

    void VulkanTexture2D::create_image_view() {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = reinterpret_cast<VkImage>(m_image);
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = VK_FORMAT_R8G8B8A8_UNORM;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.baseMipLevel = 0;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.baseArrayLayer = 0;
        view.subresourceRange.layerCount = 1;

        VkImageView iv = VK_NULL_HANDLE;
        VkResult r = vkCreateImageView(reinterpret_cast<VkDevice>(m_device), &view, nullptr, &iv);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateImageView failed");

        m_image_view = reinterpret_cast<void*>(iv);
    }

    void VulkanTexture2D::create_sampler() {
        VkSamplerCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;

        auto& current_af = Settings::get().renderer.anisotropic_filtering_level;

        // Defaults
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
        si.anisotropyEnable = VK_FALSE;
        si.maxAnisotropy    = current_af;

        using TextureFilter = Honey::RendererSettings::TextureFilter;
        TextureFilter filter = Settings::get().renderer.texture_filter;

        switch (filter) {
        case TextureFilter::nearest:
            si.magFilter  = VK_FILTER_NEAREST;
            si.minFilter  = VK_FILTER_NEAREST;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            break;

        case TextureFilter::linear:
            si.magFilter  = VK_FILTER_LINEAR;
            si.minFilter  = VK_FILTER_LINEAR;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            break;

        case TextureFilter::anisotropic:
            float current_af = Settings::get().renderer.anisotropic_filtering_level;
            si.magFilter  = VK_FILTER_LINEAR;
            si.minFilter  = VK_FILTER_LINEAR;
            si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
            si.anisotropyEnable = VK_TRUE;
            si.maxAnisotropy    = current_af;
            break;
        }

        VkSampler sampler = VK_NULL_HANDLE;
        VkResult r = vkCreateSampler(reinterpret_cast<VkDevice>(m_device), &si, nullptr, &sampler);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateSampler failed");

        m_sampler = reinterpret_cast<void*>(sampler);
    }

    void VulkanTexture2D::transition_image_layout(uint32_t old_layout, uint32_t new_layout) {
        HN_CORE_ASSERT(m_backend, "VulkanTexture2D::transition_image_layout: backend is null");

        m_backend->immediate_submit([&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = static_cast<VkImageLayout>(old_layout);
            barrier.newLayout = static_cast<VkImageLayout>(new_layout);
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = reinterpret_cast<VkImage>(m_image);
            barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            barrier.subresourceRange.baseMipLevel = 0;
            barrier.subresourceRange.levelCount = 1;
            barrier.subresourceRange.baseArrayLayer = 0;
            barrier.subresourceRange.layerCount = 1;

            VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
            VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;

            if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
                dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            } else if (old_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
                barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                src_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
                dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
            } else {
                HN_CORE_ASSERT(false, "Unsupported image layout transition");
            }

            vkCmdPipelineBarrier(cmd,
                                 src_stage, dst_stage,
                                 0,
                                 0, nullptr,
                                 0, nullptr,
                                 1, &barrier);
        });

        m_current_layout = new_layout;
    }

    void VulkanTexture2D::copy_buffer_to_image(void* staging_buffer) {
        HN_CORE_ASSERT(m_backend, "VulkanTexture2D::copy_buffer_to_image: backend is null");

        m_backend->immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferImageCopy region{};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = 0;
            region.imageSubresource.baseArrayLayer = 0;
            region.imageSubresource.layerCount = 1;
            region.imageOffset = {0, 0, 0};
            region.imageExtent = { m_width, m_height, 1 };

            vkCmdCopyBufferToImage(cmd,
                                   reinterpret_cast<VkBuffer>(staging_buffer),
                                   reinterpret_cast<VkImage>(m_image),
                                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                   1,
                                   &region);
        });
    }

    void VulkanTexture2D::init_from_pixels_rgba8(const void* rgba_pixels,
                                                     uint32_t width,
                                                     uint32_t height) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(rgba_pixels, "VulkanTexture2D: pixels null");

        create_image(width, height);
        create_image_view();
        create_sampler();

        HN_CORE_ASSERT(m_image != nullptr, "init_from_pixels: m_image is null");

        const uint32_t size = width * height * 4;

        // Streaming-friendly path: prefer using backend's staging ring if available.
        if (m_backend && m_backend->initialized()) {
            VulkanBackend::ImageUploadDesc desc{};
            desc.dstImage       = reinterpret_cast<VkImage>(m_image);
            desc.width          = width;
            desc.height         = height;
            desc.mipLevel       = 0;
            desc.arrayLayer     = 0;
            desc.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
            desc.finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            desc.srcData        = rgba_pixels;
            desc.size           = size;

            m_backend->queue_image_upload(desc);
            m_current_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            return;
        }

        // Fallback: old per-image staging + immediate_submit.
        void* staging_buffer = nullptr;
        void* staging_memory = nullptr;
        create_staging_buffer(size, staging_buffer, staging_memory);

        void* mapped = nullptr;
        VkResult r = vkMapMemory(reinterpret_cast<VkDevice>(m_device),
                                 reinterpret_cast<VkDeviceMemory>(staging_memory),
                                 0, size, 0, &mapped);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory failed for staging buffer");
        std::memcpy(mapped, rgba_pixels, size);
        vkUnmapMemory(reinterpret_cast<VkDevice>(m_device),
                      reinterpret_cast<VkDeviceMemory>(staging_memory));

        transition_image_layout(VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        copy_buffer_to_image(staging_buffer);
        transition_image_layout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        destroy_buffer(staging_buffer, staging_memory);
    }

    void VulkanTexture2D::ensure_imgui_texture_registered() {
        if (m_imgui_texture_id != 0)
            return; // already registered

        // ImGui Vulkan backend MUST already be initialized at this point
        VkSampler sampler = reinterpret_cast<VkSampler>(m_sampler);
        VkImageView view  = reinterpret_cast<VkImageView>(m_image_view);
        VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorSet set = ImGui_ImplVulkan_AddTexture(
            sampler,
            view,
            layout
        );

        m_imgui_texture_id = static_cast<ImTextureID>(
            reinterpret_cast<uintptr_t>(set)
        );
    }

    ImTextureID VulkanTexture2D::get_imgui_texture_id() {
        ensure_imgui_texture_registered();
        return m_imgui_texture_id;
    }

    void VulkanTexture2D::refresh_sampler() {
        VkDevice device = reinterpret_cast<VkDevice>(m_device);
        if (m_sampler && device) {
            vkDestroySampler(device, reinterpret_cast<VkSampler>(m_sampler), nullptr);
            m_sampler = nullptr;
            HN_CORE_INFO("Refreshing sampler for texture '{}'", m_path);
        }
        create_sampler();
    }

    void VulkanTexture2D::resize(uint32_t width, uint32_t height) {
        HN_PROFILE_FUNCTION();
        if (width == 0 || height == 0)
            return;

        if (width == m_width && height == m_height)
            return; // nothing to do

        VkDevice dev = reinterpret_cast<VkDevice>(m_device);
        if (!dev)
            return;

        // Destroy old resources
        if (m_imgui_texture_id != 0 && m_backend && m_backend->imgui_initialized()) {
            ImGui_ImplVulkan_RemoveTexture(
                reinterpret_cast<VkDescriptorSet>(m_imgui_texture_id)
            );
            m_imgui_texture_id = 0;
        }
        if (m_sampler) {
            vkDestroySampler(dev, reinterpret_cast<VkSampler>(m_sampler), nullptr);
            m_sampler = nullptr;
        }
        if (m_image_view) {
            vkDestroyImageView(dev, reinterpret_cast<VkImageView>(m_image_view), nullptr);
            m_image_view = nullptr;
        }
        if (m_image) {
            vkDestroyImage(dev, reinterpret_cast<VkImage>(m_image), nullptr);
            m_image = nullptr;
        }
        if (m_image_memory) {
            vkFreeMemory(dev, reinterpret_cast<VkDeviceMemory>(m_image_memory), nullptr);
            m_image_memory = nullptr;
        }

        m_width  = width;
        m_height = height;

        // Recreate image + view + sampler (no data yet).
        std::vector<uint8_t> empty(m_width * m_height * 4, 0);
        init_from_pixels_rgba8(empty.data(), m_width, m_height);
    }
}