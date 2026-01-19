#include "hnpch.h"
#include "vk_buffer.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

namespace Honey {

    static uint32_t find_memory_type(VkPhysicalDevice phys, uint32_t type_filter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mem_props{};
        vkGetPhysicalDeviceMemoryProperties(phys, &mem_props);

        for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
            if ((type_filter & (1u << i)) && ((mem_props.memoryTypes[i].propertyFlags & props) == props))
                return i;
        }

        HN_CORE_ASSERT(false, "Failed to find suitable Vulkan memory type");
        return 0;
    }

    static void create_buffer(VkDevice dev,
                              VkPhysicalDevice phys,
                              VkDeviceSize size,
                              VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props,
                              VkBuffer& out_buffer,
                              VkDeviceMemory& out_memory) {
        VkBufferCreateInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult r = vkCreateBuffer(dev, &bi, nullptr, &out_buffer);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkCreateBuffer failed");

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(dev, out_buffer, &req);

        VkMemoryAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = find_memory_type(phys, req.memoryTypeBits, props);

        r = vkAllocateMemory(dev, &ai, nullptr, &out_memory);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkAllocateMemory failed");

        r = vkBindBufferMemory(dev, out_buffer, out_memory, 0);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkBindBufferMemory failed");
    }

    static void upload_to_buffer(VkDevice dev, VkDeviceMemory mem, const void* data, VkDeviceSize size) {
        void* mapped = nullptr;
        VkResult r = vkMapMemory(dev, mem, 0, size, 0, &mapped);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory failed");
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(dev, mem);
    }

    VulkanVertexBuffer::VulkanVertexBuffer(VkDevice device, VkPhysicalDevice phys, uint32_t size)
        : m_device_raw(device), m_phys_raw(phys) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device_raw && m_phys_raw, "VulkanVertexBuffer: invalid device/physical device");
        allocate(size, nullptr);
    }

    VulkanVertexBuffer::VulkanVertexBuffer(VkDevice device, VkPhysicalDevice phys, float* vertices, uint32_t size)
        : m_device_raw(device), m_phys_raw(phys) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device_raw && m_phys_raw, "VulkanVertexBuffer: invalid device/physical device");
        allocate(size, vertices);
    }

    VulkanVertexBuffer::~VulkanVertexBuffer() {
        HN_PROFILE_FUNCTION();
        if (!m_device_raw) return;

        if (m_buffer) vkDestroyBuffer(m_device_raw, reinterpret_cast<VkBuffer>(m_buffer), nullptr);
        if (m_memory) vkFreeMemory(m_device_raw, reinterpret_cast<VkDeviceMemory>(m_memory), nullptr);

        m_buffer = nullptr;
        m_memory = nullptr;
        m_device_raw = nullptr;
        m_phys_raw = nullptr;
    }

    void VulkanVertexBuffer::allocate(uint32_t size, const void* initial_data) {
        m_size = size;

        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;

        create_buffer(m_device_raw, m_phys_raw, size,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buf, mem);

        m_buffer = reinterpret_cast<void*>(buf);
        m_memory = reinterpret_cast<void*>(mem);

        if (initial_data) {
            upload_to_buffer(m_device_raw, mem, initial_data, size);
        }
    }

    void VulkanVertexBuffer::set_data(const void* data, uint32_t size) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(size <= m_size, "VulkanVertexBuffer::set_data overflow (size > buffer size)");
        upload_to_buffer(m_device_raw, reinterpret_cast<VkDeviceMemory>(m_memory), data, size);
    }

    VulkanIndexBuffer::VulkanIndexBuffer(VkDevice device, VkPhysicalDevice phys, uint32_t* indices, uint32_t count)
        : m_count(count), m_device_raw(device), m_phys_raw(phys) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device_raw && m_phys_raw, "VulkanIndexBuffer: invalid device/physical device");
        allocate(count * sizeof(uint32_t), indices);
    }

    VulkanIndexBuffer::~VulkanIndexBuffer() {
        HN_PROFILE_FUNCTION();
        if (!m_device_raw) return;

        if (m_buffer) vkDestroyBuffer(m_device_raw, reinterpret_cast<VkBuffer>(m_buffer), nullptr);
        if (m_memory) vkFreeMemory(m_device_raw, reinterpret_cast<VkDeviceMemory>(m_memory), nullptr);

        m_buffer = nullptr;
        m_memory = nullptr;
        m_device_raw = nullptr;
        m_phys_raw = nullptr;
    }

    void VulkanIndexBuffer::allocate(uint32_t bytes, const void* initial_data) {
        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;

        create_buffer(m_device_raw, m_phys_raw, bytes,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buf, mem);

        m_buffer = reinterpret_cast<void*>(buf);
        m_memory = reinterpret_cast<void*>(mem);

        upload_to_buffer(m_device_raw, mem, initial_data, bytes);
    }

    VulkanUniformBuffer::VulkanUniformBuffer(VkDevice device, VkPhysicalDevice phys, uint32_t size, uint32_t binding)
        : m_device_raw(device), m_phys_raw(phys), m_binding(binding) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device_raw && m_phys_raw, "VulkanUniformBuffer: invalid device/physical device");
        allocate(size);
    }

    VulkanUniformBuffer::~VulkanUniformBuffer() {
        HN_PROFILE_FUNCTION();
        if (!m_device_raw) return;

        if (m_buffer) vkDestroyBuffer(m_device_raw, reinterpret_cast<VkBuffer>(m_buffer), nullptr);
        if (m_memory) vkFreeMemory(m_device_raw, reinterpret_cast<VkDeviceMemory>(m_memory), nullptr);

        m_buffer = nullptr;
        m_memory = nullptr;
        m_device_raw = nullptr;
        m_phys_raw = nullptr;
    }

    void VulkanUniformBuffer::allocate(uint32_t size) {
        m_size = size;

        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;

        create_buffer(m_device_raw, m_phys_raw, size,
                      VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      buf, mem);

        m_buffer = reinterpret_cast<void*>(buf);
        m_memory = reinterpret_cast<void*>(mem);
    }

    void VulkanUniformBuffer::set_data(uint32_t size, const void* data) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(size <= m_size, "VulkanUniformBuffer::set_data overflow (size > buffer size)");
        upload_to_buffer(m_device_raw, reinterpret_cast<VkDeviceMemory>(m_memory), data, size);
    }

} // namespace Honey