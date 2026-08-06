#include "hnpch.h"
#include "vk_icon_globals.h"

namespace Honey {

    void VulkanIconGlobals::init(VulkanBackend* backend) {
        HN_CORE_ASSERT(backend, "[VulkanIconGlobals] backend cannot be null.");
        HN_CORE_ASSERT(!m_backend, "[VulkanIconGlobals] init called twice.");
        
        m_backend = backend;
        m_device = backend->get_device();
        m_physical_device = backend->get_physical_device();
        
        create_globals_resources();
        register_heap_bindings();
    }

    void VulkanIconGlobals::shutdown() {
        if (!m_device) return;

        if (m_icon_buffer) {
            vkDestroyBuffer(m_device, m_icon_buffer, nullptr);
            vkFreeMemory(m_device, m_icon_alloc, nullptr);
            m_icon_buffer = nullptr;
            m_icon_alloc = nullptr;
        }

        m_device = VK_NULL_HANDLE;
        m_physical_device = VK_NULL_HANDLE;
        m_backend = nullptr;
    }

    VectorIcon::IconSlot VulkanIconGlobals::allocate_icon_slot(uint32_t band_count, uint32_t curve_count) {
        std::scoped_lock lock(m_alloc_mutex);
        const uint64_t key = (uint64_t(band_count) << 32) | curve_count;
        auto it = m_freelist.find(key);
        if (it != m_freelist.end() && !it->second.empty()) {
            VectorIcon::IconSlot s = it->second.back();
            it->second.pop_back();
            return s;
        }
        HN_CORE_ASSERT(m_band_cursor + band_count <= k_total_band_entries, "[VulkanIconGlobals] Icon band table exhausted.");
        HN_CORE_ASSERT(m_curve_cursor + curve_count <= k_total_curves, "[VulkanIconGlobals] Icon curve buffer exhausted.");
        VectorIcon::IconSlot s{ m_band_cursor, band_count, m_curve_cursor, curve_count };
        m_band_cursor += band_count;
        m_curve_cursor += curve_count;
        return s;
    }

    void VulkanIconGlobals::free_icon_slot(const VectorIcon::IconSlot& slot) {
        std::scoped_lock lock(m_alloc_mutex);
        const uint64_t key = (uint64_t(slot.band_count) << 32) | slot.curve_count;
        m_freelist[key].push_back(slot);
    }

    void VulkanIconGlobals::upload_icon_data(const BandEntry* bands, uint32_t band_count, uint32_t band_offset,
        const QuadBezier* curves, uint32_t curve_count, uint32_t curve_offset) {
        const VkDeviceSize band_bytes  = VkDeviceSize(band_count)  * sizeof(BandEntry);
        const VkDeviceSize curve_bytes = VkDeviceSize(curve_count) * sizeof(QuadBezier);
        const VkDeviceSize total_bytes = band_bytes + curve_bytes;

        // create_staging_buffer-equivalent: HOST_VISIBLE|HOST_COHERENT, TRANSFER_SRC, one-shot.
        // Mirrors VulkanTexture2D::create_staging_buffer (vk_texture.cpp:304-333).
        VkBufferCreateInfo staging_bi{};
        staging_bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        staging_bi.size = total_bytes;
        staging_bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        staging_bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkBuffer staging = VK_NULL_HANDLE;
        VkResult res = vkCreateBuffer(m_device, &staging_bi, nullptr, &staging);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanIconGlobals] vkCreateBuffer (staging) failed");

        VkMemoryRequirements staging_req{};
        vkGetBufferMemoryRequirements(m_device, staging, &staging_req);

        VkMemoryAllocateInfo staging_ai{};
        staging_ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        staging_ai.allocationSize = staging_req.size;
        staging_ai.memoryTypeIndex = VulkanUtils::find_memory_type(m_physical_device, staging_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        VkDeviceMemory staging_mem = VK_NULL_HANDLE;
        res = vkAllocateMemory(m_device, &staging_ai, nullptr, &staging_mem);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanIconGlobals] vkAllocateMemory (staging) failed");

        res = vkBindBufferMemory(m_device, staging, staging_mem, 0);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanIconGlobals] vkBindBufferMemory (staging) failed");

        void* mapped = nullptr;
        vkMapMemory(m_device, staging_mem, 0, total_bytes, 0, &mapped);
        std::memcpy(mapped, bands, band_bytes);
        std::memcpy(static_cast<uint8_t*>(mapped) + band_bytes, curves, curve_bytes);
        vkUnmapMemory(m_device, staging_mem);

        m_backend->immediate_submit("UploadIconData", [&](VkCommandBuffer cmd) {
            VkBufferCopy band_region{ 0, VkDeviceSize(band_offset) * sizeof(BandEntry), band_bytes };
            vkCmdCopyBuffer(cmd, staging, m_icon_buffer, 1, &band_region);

            VkBufferCopy curve_region{ band_bytes,
                m_curve_region_offset + VkDeviceSize(curve_offset) * sizeof(QuadBezier), curve_bytes };
            vkCmdCopyBuffer(cmd, staging, m_icon_buffer, 1, &curve_region);

            // Make the copy visible to shader reads in *later* submissions — the equivalent of the
            // TRANSFER_WRITE->SHADER_READ_ONLY layout-transition barrier vk_texture.cpp does for images.
            VkMemoryBarrier post{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            post.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            post.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0, 1, &post, 0, nullptr, 0, nullptr);
        });

        vkDestroyBuffer(m_device, staging, nullptr);
        vkFreeMemory(m_device, staging_mem, nullptr);
    }

    void VulkanIconGlobals::create_globals_resources() {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &props);
        const VkDeviceSize align = props.limits.minStorageBufferOffsetAlignment;

        const VkDeviceSize band_region_size = VkDeviceSize(k_total_band_entries) * sizeof(BandEntry);
        m_curve_region_offset = VulkanUtils::align_up(band_region_size, align);
        const VkDeviceSize curve_region_size = VkDeviceSize(k_total_curves) * sizeof(QuadBezier);
        const VkDeviceSize total_size = m_curve_region_offset + curve_region_size;

        const VkDevice dev = m_device;
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = total_size;
        bi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
        | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT
        | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult res = vkCreateBuffer(dev, &bi, nullptr, &m_icon_buffer);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to create globals buffer");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev, m_icon_buffer, &req);

        VkMemoryAllocateFlagsInfo flags{};
        flags.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO;
        flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.pNext = &flags;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = VulkanUtils::find_memory_type(m_physical_device, req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        res = vkAllocateMemory(dev, &ai, nullptr, &m_icon_alloc);
        HN_CORE_ASSERT(res == VK_SUCCESS, "[VulkanRendererGlobals] Failed to allocate memory for globals heap");
        vkBindBufferMemory(dev, m_icon_buffer, m_icon_alloc, 0);
    }

    void VulkanIconGlobals::register_heap_bindings() {
        VkBufferDeviceAddressInfo addr{ VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
        addr.buffer = m_icon_buffer;
        const VkDeviceAddress base = vkGetBufferDeviceAddress(m_device, &addr);

        auto* heap = m_backend->get_descriptor_heap();

        auto band_slot = heap->allocate_persistent_resource(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
        heap->write_buffer(band_slot, 0, base,
            VkDeviceSize(k_total_band_entries) * sizeof(BandEntry), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        heap->register_global_binding(k_global_bindings[(size_t)GlobalBinding::IconBandTable].shader_binding, band_slot);

        auto curve_slot = heap->allocate_persistent_resource(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
        heap->write_buffer(curve_slot, 0, base + m_curve_region_offset,
            VkDeviceSize(k_total_curves) * sizeof(QuadBezier), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        heap->register_global_binding(k_global_bindings[(size_t)GlobalBinding::IconCurves].shader_binding, curve_slot);
    }

}
