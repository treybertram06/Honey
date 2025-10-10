#include "hnpch.h"
#include "vk_buffer.h"
#include "vk_context.h"

namespace Honey {

    // One-shot buffer copy helper
    void copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
        HN_PROFILE_FUNCTION();

        VkDevice device = VulkanContext::get_device();

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = VulkanContext::get_command_pool();
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        HN_CORE_ASSERT(vkAllocateCommandBuffers(device, &allocInfo, &cmd) == VK_SUCCESS,
                       "Failed to allocate command buffer for copy!");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        HN_CORE_ASSERT(vkBeginCommandBuffer(cmd, &beginInfo) == VK_SUCCESS,
                       "Failed to begin copy command buffer!");

        VkBufferCopy region{};
        region.srcOffset = 0;
        region.dstOffset = 0;
        region.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &region);

        HN_CORE_ASSERT(vkEndCommandBuffer(cmd) == VK_SUCCESS,
                       "Failed to end copy command buffer!");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        HN_CORE_ASSERT(vkQueueSubmit(VulkanContext::get_graphics_queue(), 1, &submitInfo, VK_NULL_HANDLE) == VK_SUCCESS,
                       "Failed to submit copy command buffer!");
        vkQueueWaitIdle(VulkanContext::get_graphics_queue());

        vkFreeCommandBuffers(device, VulkanContext::get_command_pool(), 1, &cmd);
    }

    // ============================
    // Vertex Buffer
    // ============================

    VulkanVertexBuffer::VulkanVertexBuffer(uint32_t size)
        : m_size(size)
    {
        HN_PROFILE_FUNCTION();

        // Host-visible VB (good for dynamic updates). If you'll also copy into it, add TRANSFER_DST.
        create_buffer(
            size,
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            m_buffer, m_buffer_memory
        );
    }

    VulkanVertexBuffer::VulkanVertexBuffer(float* vertices, uint32_t size)
        : m_size(size)
    {
        HN_PROFILE_FUNCTION();

        // Staging
        VkBuffer staging_buffer;
        VkDeviceMemory staging_buffer_memory;
        create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging_buffer, staging_buffer_memory);

        void* data = nullptr;
        HN_CORE_ASSERT(vkMapMemory(VulkanContext::get_device(), staging_buffer_memory, 0, size, 0, &data) == VK_SUCCESS,
                       "Failed to map staging buffer!");
        memcpy(data, vertices, size);
        vkUnmapMemory(VulkanContext::get_device(), staging_buffer_memory);

        // Device-local VB
        create_buffer(size,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      m_buffer, m_buffer_memory);

        copy_buffer(staging_buffer, m_buffer, size);

        vkDestroyBuffer(VulkanContext::get_device(), staging_buffer, nullptr);
        vkFreeMemory(VulkanContext::get_device(), staging_buffer_memory, nullptr);
    }

    VulkanVertexBuffer::~VulkanVertexBuffer() {
        HN_PROFILE_FUNCTION();

        if (m_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(VulkanContext::get_device(), m_buffer, nullptr);
            m_buffer = VK_NULL_HANDLE;
        }
        if (m_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(VulkanContext::get_device(), m_buffer_memory, nullptr);
            m_buffer_memory = VK_NULL_HANDLE;
        }
    }

    void VulkanVertexBuffer::bind() const {
        // Binding is done during command recording:
        // vkCmdBindVertexBuffers(cmd, firstBinding=0, bindingCount=1, &m_buffer, offsets=[0])
    }

    void VulkanVertexBuffer::unbind() const {
        // No-op in Vulkan
    }

    void VulkanVertexBuffer::set_data(const void* data, uint32_t size) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(size <= m_size, "Data size exceeds buffer size");

        // Try to map directly (works for HOST_VISIBLE). If that fails (DEVICE_LOCAL),
        // fall back to staging copy.
        void* mapped = nullptr;
        VkResult mapRes = vkMapMemory(VulkanContext::get_device(), m_buffer_memory, 0, size, 0, &mapped);
        if (mapRes == VK_SUCCESS) {
            memcpy(mapped, data, size);
            vkUnmapMemory(VulkanContext::get_device(), m_buffer_memory);
            return;
        }

        // Fallback: staging upload
        VkBuffer staging_buffer;
        VkDeviceMemory staging_memory;
        create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging_buffer, staging_memory);

        void* stagingPtr = nullptr;
        HN_CORE_ASSERT(vkMapMemory(VulkanContext::get_device(), staging_memory, 0, size, 0, &stagingPtr) == VK_SUCCESS,
                       "Failed to map staging buffer in set_data!");
        memcpy(stagingPtr, data, size);
        vkUnmapMemory(VulkanContext::get_device(), staging_memory);

        copy_buffer(staging_buffer, m_buffer, size);

        vkDestroyBuffer(VulkanContext::get_device(), staging_buffer, nullptr);
        vkFreeMemory(VulkanContext::get_device(), staging_memory, nullptr);
    }

    void VulkanVertexBuffer::create_buffer(
        VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
        VkBuffer& buffer, VkDeviceMemory& buffer_memory
    ) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(VulkanContext::get_device(), &buffer_info, nullptr, &buffer);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create buffer!");

        VkMemoryRequirements mem_requirements{};
        vkGetBufferMemoryRequirements(VulkanContext::get_device(), buffer, &mem_requirements);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

        result = vkAllocateMemory(VulkanContext::get_device(), &alloc_info, nullptr, &buffer_memory);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate buffer memory!");

        vkBindBufferMemory(VulkanContext::get_device(), buffer, buffer_memory, 0);
    }

    uint32_t VulkanVertexBuffer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties mem_properties{};
        vkGetPhysicalDeviceMemoryProperties(VulkanContext::get_physical_device(), &mem_properties);

        for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
            if ((type_filter & (1 << i)) &&
                (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        HN_CORE_ASSERT(false, "Failed to find suitable memory type!");
        return 0;
    }

    // ============================
    // Index Buffer
    // ============================

    VulkanIndexBuffer::VulkanIndexBuffer(uint32_t* indices, uint32_t count)
        : m_count(count)
    {
        HN_PROFILE_FUNCTION();

        VkDeviceSize size = sizeof(uint32_t) * count;

        // Staging
        VkBuffer staging_buffer;
        VkDeviceMemory staging_memory;
        create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      staging_buffer, staging_memory);

        void* data = nullptr;
        HN_CORE_ASSERT(vkMapMemory(VulkanContext::get_device(), staging_memory, 0, size, 0, &data) == VK_SUCCESS,
                       "Failed to map index staging buffer!");
        memcpy(data, indices, (size_t)size);
        vkUnmapMemory(VulkanContext::get_device(), staging_memory);

        // Device-local IB
        create_buffer(size,
                      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      m_buffer, m_buffer_memory);

        copy_buffer(staging_buffer, m_buffer, size);

        vkDestroyBuffer(VulkanContext::get_device(), staging_buffer, nullptr);
        vkFreeMemory(VulkanContext::get_device(), staging_memory, nullptr);
    }

    VulkanIndexBuffer::~VulkanIndexBuffer() {
        HN_PROFILE_FUNCTION();

        if (m_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(VulkanContext::get_device(), m_buffer, nullptr);
            m_buffer = VK_NULL_HANDLE;
        }
        if (m_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(VulkanContext::get_device(), m_buffer_memory, nullptr);
            m_buffer_memory = VK_NULL_HANDLE;
        }
    }

    void VulkanIndexBuffer::bind() const {
        // Binding is done during command recording:
        // vkCmdBindIndexBuffer(cmd, m_buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    void VulkanIndexBuffer::unbind() const {
        // No-op
    }

    void VulkanIndexBuffer::create_buffer(
        VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
        VkBuffer& buffer, VkDeviceMemory& buffer_memory
    ) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(VulkanContext::get_device(), &buffer_info, nullptr, &buffer);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create index buffer!");

        VkMemoryRequirements mem_requirements{};
        vkGetBufferMemoryRequirements(VulkanContext::get_device(), buffer, &mem_requirements);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

        result = vkAllocateMemory(VulkanContext::get_device(), &alloc_info, nullptr, &buffer_memory);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate index buffer memory!");

        vkBindBufferMemory(VulkanContext::get_device(), buffer, buffer_memory, 0);
    }

    uint32_t VulkanIndexBuffer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties mem_properties{};
        vkGetPhysicalDeviceMemoryProperties(VulkanContext::get_physical_device(), &mem_properties);

        for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
            if ((type_filter & (1 << i)) &&
                (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        HN_CORE_ASSERT(false, "Failed to find suitable memory type for index buffer!");
        return 0;
    }

    // ============================
    // Uniform Buffer
    // ============================

    VulkanUniformBuffer::VulkanUniformBuffer(uint32_t size, uint32_t binding)
        : m_size(size), m_binding(binding)
    {
        HN_PROFILE_FUNCTION();

        // Uniform buffers should be host-visible (often persistently mapped)
        create_buffer(size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_buffer, m_buffer_memory);

        // Persistent map for quick updates
        HN_CORE_ASSERT(vkMapMemory(VulkanContext::get_device(), m_buffer_memory, 0, size, 0, &m_mapped_memory) == VK_SUCCESS,
                       "Failed to map uniform buffer memory!");
    }

    VulkanUniformBuffer::~VulkanUniformBuffer() {
        HN_PROFILE_FUNCTION();

        if (m_mapped_memory) {
            vkUnmapMemory(VulkanContext::get_device(), m_buffer_memory);
            m_mapped_memory = nullptr;
        }
        if (m_buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(VulkanContext::get_device(), m_buffer, nullptr);
            m_buffer = VK_NULL_HANDLE;
        }
        if (m_buffer_memory != VK_NULL_HANDLE) {
            vkFreeMemory(VulkanContext::get_device(), m_buffer_memory, nullptr);
            m_buffer_memory = VK_NULL_HANDLE;
        }
    }

    void VulkanUniformBuffer::bind() const {
        // Descriptor set binding happens elsewhere (pipeline layout / descriptor writes).
        // This method can remain a no-op.
    }

    void VulkanUniformBuffer::unbind() const {
        // No-op
    }

    void VulkanUniformBuffer::set_data(uint32_t size, const void* data) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(size <= m_size, "UBO write exceeds buffer size");
        HN_CORE_ASSERT(m_mapped_memory != nullptr, "UBO memory is not mapped");
        memcpy(m_mapped_memory, data, size);
        // HOST_COHERENT removes the need for explicit flush.
        // If you switch to NON_COHERENT, add vkFlushMappedMemoryRanges.
    }

    void VulkanUniformBuffer::create_buffer(
        VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
        VkBuffer& buffer, VkDeviceMemory& buffer_memory
    ) {
        VkBufferCreateInfo buffer_info{};
        buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer_info.size = size;
        buffer_info.usage = usage;
        buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult result = vkCreateBuffer(VulkanContext::get_device(), &buffer_info, nullptr, &buffer);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to create uniform buffer!");

        VkMemoryRequirements mem_requirements{};
        vkGetBufferMemoryRequirements(VulkanContext::get_device(), buffer, &mem_requirements);

        VkMemoryAllocateInfo alloc_info{};
        alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.allocationSize = mem_requirements.size;
        alloc_info.memoryTypeIndex = find_memory_type(mem_requirements.memoryTypeBits, properties);

        result = vkAllocateMemory(VulkanContext::get_device(), &alloc_info, nullptr, &buffer_memory);
        HN_CORE_ASSERT(result == VK_SUCCESS, "Failed to allocate uniform buffer memory!");

        vkBindBufferMemory(VulkanContext::get_device(), buffer, buffer_memory, 0);
    }

    uint32_t VulkanUniformBuffer::find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties) {
        VkPhysicalDeviceMemoryProperties mem_properties{};
        vkGetPhysicalDeviceMemoryProperties(VulkanContext::get_physical_device(), &mem_properties);

        for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
            if ((type_filter & (1 << i)) &&
                (mem_properties.memoryTypes[i].propertyFlags & properties) == properties) {
                return i;
            }
        }
        HN_CORE_ASSERT(false, "Failed to find suitable memory type for uniform buffer!");
        return 0;
    }
}