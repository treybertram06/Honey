#include "hnpch.h"
#include "vk_buffer.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include "Honey/core/engine.h"
#include "platform/vulkan/vk_backend.h"

namespace Honey {

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

    static void create_buffer(VkDevice dev,
                              VkPhysicalDevice phys,
                              VkDeviceSize size,
                              VkBufferUsageFlags usage,
                              VkMemoryPropertyFlags props,
                              VkBuffer& out_buffer,
                              VkDeviceMemory& out_memory) {
        HN_PROFILE_FUNCTION();
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
        HN_PROFILE_FUNCTION();
        void* mapped = nullptr;
        VkResult r = vkMapMemory(dev, mem, 0, size, 0, &mapped);
        HN_CORE_ASSERT(r == VK_SUCCESS, "vkMapMemory failed");
        std::memcpy(mapped, data, static_cast<size_t>(size));
        vkUnmapMemory(dev, mem);
    }

    static void copy_buffer_immediate(VulkanBackend& backend, VkBuffer src, VkBuffer dst, VkDeviceSize size) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(src && dst && size > 0, "copy_buffer_immediate: invalid args");

        backend.immediate_submit([&](VkCommandBuffer cmd) {
            VkBufferCopy region{};
            region.srcOffset = 0;
            region.dstOffset = 0;
            region.size = size;
            vkCmdCopyBuffer(cmd, src, dst, 1, &region);
        });
    }

    static void create_device_local_buffer_with_staging(
        VkDevice dev,
        VkPhysicalDevice phys,
        VulkanBackend& backend,
        const void* initial_data,
        VkDeviceSize size,
        VkBufferUsageFlags final_usage,
        VkBuffer& out_buffer,
        VkDeviceMemory& out_memory
    ) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(dev && phys, "create_device_local_buffer_with_staging: invalid device/physical device");
        HN_CORE_ASSERT(size > 0, "create_device_local_buffer_with_staging: size must be > 0");
        HN_CORE_ASSERT(initial_data, "create_device_local_buffer_with_staging: initial_data is null");

        // 1) staging (CPU-visible)
        VkBuffer staging_buf = VK_NULL_HANDLE;
        VkDeviceMemory staging_mem = VK_NULL_HANDLE;

        create_buffer(
            dev, phys, size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging_buf, staging_mem
        );

        upload_to_buffer(dev, staging_mem, initial_data, size);

        // 2) final (GPU-local)
        create_buffer(
            dev, phys, size,
            final_usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            out_buffer, out_memory
        );

        // 3) copy
        copy_buffer_immediate(backend, staging_buf, out_buffer, size);

        // 4) cleanup staging
        vkDestroyBuffer(dev, staging_buf, nullptr);
        vkFreeMemory(dev, staging_mem, nullptr);
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
        HN_PROFILE_FUNCTION();
        m_size = size;

        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;

        if (initial_data) {
            auto& backend = Application::get().get_vulkan_backend();
            create_device_local_buffer_with_staging(
                m_device_raw,
                m_phys_raw,
                backend,
                initial_data,
                size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                buf,
                mem
            );
        } else {
            // No initial data: keep it simple for now (CPU-visible dynamic buffer)
            create_buffer(
                m_device_raw, m_phys_raw, size,
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                buf, mem
            );
        }

        m_buffer = reinterpret_cast<void*>(buf);
        m_memory = reinterpret_cast<void*>(mem);
    }

    void VulkanVertexBuffer::set_data(const void* data, uint32_t size) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(size <= m_size, "VulkanVertexBuffer::set_data overflow (size > buffer size)");
        HN_CORE_ASSERT(data, "VulkanVertexBuffer::set_data data is null");

        // NOTE: If this buffer was allocated DEVICE_LOCAL (staged), this will not work.
        // For now, keep your usage pattern: use set_data on "dynamic" buffers only.
        // When you need dynamic meshes, add a separate DynamicVulkanVertexBuffer or keep a staging+copy path here too.
        upload_to_buffer(m_device_raw, reinterpret_cast<VkDeviceMemory>(m_memory), data, size);
    }

    VulkanIndexBuffer::VulkanIndexBuffer(VkDevice device, VkPhysicalDevice phys, uint32_t* indices, uint32_t count)
        : m_count(count), m_device_raw(device), m_phys_raw(phys), m_type(VK_INDEX_TYPE_UINT32) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device_raw && m_phys_raw, "VulkanIndexBuffer: invalid device/physical device");
        allocate(count * sizeof(uint32_t), indices);
    }

    VulkanIndexBuffer::VulkanIndexBuffer(VkDevice device, VkPhysicalDevice phys, uint16_t* indices, uint32_t count)
        : m_count(count), m_device_raw(device), m_phys_raw(phys), m_type(VK_INDEX_TYPE_UINT16) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(m_device_raw && m_phys_raw, "VulkanIndexBuffer: invalid device/physical device");
        allocate(count * sizeof(uint16_t), indices);
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
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(initial_data, "VulkanIndexBuffer::allocate requires initial_data");

        VkBuffer buf = VK_NULL_HANDLE;
        VkDeviceMemory mem = VK_NULL_HANDLE;

        auto& backend = Application::get().get_vulkan_backend();
        create_device_local_buffer_with_staging(
            m_device_raw,
            m_phys_raw,
            backend,
            initial_data,
            bytes,
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            buf,
            mem
        );

        m_buffer = reinterpret_cast<void*>(buf);
        m_memory = reinterpret_cast<void*>(mem);
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
        HN_PROFILE_FUNCTION();
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