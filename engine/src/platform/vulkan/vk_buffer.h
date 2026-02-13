#pragma once

#include "Honey/renderer/buffer.h"
#include "vk_context.h"

namespace Honey {

    class VulkanVertexBuffer : public VertexBuffer {
    public:
        VulkanVertexBuffer(VkDevice device, VkPhysicalDevice phys, uint32_t size);
        VulkanVertexBuffer(VkDevice device, VkPhysicalDevice phys, float* vertices, uint32_t size);
        ~VulkanVertexBuffer() override;

        void bind() const override {}
        void unbind() const override {}

        void set_data(const void* data, uint32_t size) override;

        const BufferLayout& get_layout() const override { return m_layout; }
        void set_layout(const BufferLayout& layout) override { m_layout = layout; }

        void* get_vk_buffer() const { return m_buffer; }

    private:
        void allocate(uint32_t size, const void* initial_data);

        BufferLayout m_layout;
        uint32_t m_size = 0;

        VkDevice m_device_raw = nullptr;
        VkPhysicalDevice m_phys_raw = nullptr;

        void* m_buffer = nullptr; // VkBuffer
        void* m_memory = nullptr; // VkDeviceMemory
    };

    class VulkanIndexBuffer : public IndexBuffer {
    public:
        VulkanIndexBuffer(VkDevice device, VkPhysicalDevice phys, uint32_t* indices, uint32_t count);
        VulkanIndexBuffer(VkDevice device, VkPhysicalDevice phys, uint16_t* indices, uint32_t count);
        ~VulkanIndexBuffer() override;

        void bind() const override {}
        void unbind() const override {}

        uint32_t get_count() const override { return m_count; }
        VkIndexType get_type() const { return m_type; }

        void* get_vk_buffer() const { return m_buffer; }

    private:
        void allocate(uint32_t bytes, const void* initial_data);

        uint32_t m_count = 0;
        VkIndexType m_type;

        VkDevice m_device_raw = nullptr;
        VkPhysicalDevice m_phys_raw = nullptr;

        void* m_buffer = nullptr; // VkBuffer
        void* m_memory = nullptr; // VkDeviceMemory
    };

    class VulkanUniformBuffer : public UniformBuffer {
    public:
        VulkanUniformBuffer(VkDevice device, VkPhysicalDevice phys, uint32_t size, uint32_t binding);
        ~VulkanUniformBuffer() override;

        void bind() const override {}
        void unbind() const override {}

        void set_data(uint32_t size, const void* data) override;

        void* get_vk_buffer() const { return m_buffer; }
        uint32_t get_binding() const { return m_binding; }
        uint32_t get_size() const { return m_size; }

    private:
        void allocate(uint32_t size);

        VkDevice m_device_raw = nullptr;
        VkPhysicalDevice m_phys_raw = nullptr;

        uint32_t m_binding = 0;
        uint32_t m_size = 0;

        void* m_buffer = nullptr; // VkBuffer
        void* m_memory = nullptr; // VkDeviceMemory
    };

} // namespace Honey