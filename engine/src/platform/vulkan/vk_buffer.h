#pragma once

#include <vulkan/vulkan_core.h>

#include "Honey/core/base.h"
#include "Honey/renderer/buffer.h"
#include "glm/fwd.hpp"

namespace Honey {

    class VulkanVertexBuffer : public VertexBuffer {
    public:
        VulkanVertexBuffer(uint32_t size);
        VulkanVertexBuffer(float* vertices, uint32_t size);
        virtual ~VulkanVertexBuffer();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual void set_data(const void *data, uint32_t size) override;

        virtual void set_layout(const BufferLayout& layout) override { m_layout = layout; }
        virtual const BufferLayout& get_layout() const override { return m_layout; }

        VkBuffer get_buffer() const { return m_buffer; }
        VkDeviceMemory get_memory() const { return m_buffer_memory; }

    private:
        void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory);
        uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_buffer_memory = VK_NULL_HANDLE;
        uint32_t m_size;
        BufferLayout m_layout;
    };

    class VulkanIndexBuffer : public IndexBuffer {
    public:
        VulkanIndexBuffer(uint32_t* indices, uint32_t count);
        virtual ~VulkanIndexBuffer();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual uint32_t get_count() const override { return m_count; }

        VkBuffer get_buffer() const { return m_buffer; }
        VkDeviceMemory get_memory() const { return m_buffer_memory; }

    private:
        void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory);
        uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_buffer_memory = VK_NULL_HANDLE;
        uint32_t m_count;
    };

    class VulkanUniformBuffer : public UniformBuffer {
    public:
        VulkanUniformBuffer(uint32_t size, uint32_t binding);
        virtual ~VulkanUniformBuffer();

        virtual void bind() const override;
        virtual void unbind() const override;

        virtual void set_data(uint32_t size, const void* data) override;

        VkBuffer get_buffer() const { return m_buffer; }
        VkDeviceMemory get_memory() const { return m_buffer_memory; }

    private:
        void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags properties, VkBuffer& buffer, VkDeviceMemory& buffer_memory);
        uint32_t find_memory_type(uint32_t type_filter, VkMemoryPropertyFlags properties);

        VkBuffer m_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_buffer_memory = VK_NULL_HANDLE;
        void* m_mapped_memory = nullptr;
        uint32_t m_size;
        uint32_t m_binding;
    };
}
