#pragma once

#include "vk_context.h"

namespace Honey {

    class VulkanClothSim {
    public:
        VulkanClothSim() = default;
        ~VulkanClothSim();

        bool init(VulkanContext* context, uint32_t width, uint32_t height);
        void shutdown();

        bool valid() const { return m_initialized; }

        void record_seed(VkCommandBuffer cmd) const;
        // Runs `substeps` simulation dispatches within a single command buffer,
        // dividing dt evenly and inserting compute barriers between passes.
        // More substeps = stiffer cloth and less stretching, at proportional GPU cost.
        void record_sim(VkCommandBuffer cmd, float dt, uint32_t frame_index, uint32_t substeps = 8);

        // Binds externally-owned state buffers (typically from FrameGraph Buffer resources).
        // Buffer A maps to ping-pong index 0, buffer B maps to ping-pong index 1.
        void set_external_state_buffers(VkBuffer buffer_a, VkBuffer buffer_b);

        void reset_ping_pong();
        void swap_ping_pong();

        VkBuffer current_read_buffer() const;
        VkBuffer current_write_buffer() const;

        uint32_t read_index() const { return m_read_index; }
        uint32_t write_index() const { return m_write_index; }
        uint32_t descriptor_set_index() const { return active_set_index(); }

        uint32_t width() const { return m_width; }
        uint32_t height() const { return m_height; }
        uint32_t particle_count() const { return m_particle_count; }

    private:
        struct BufferResource {
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
        };

        struct ComputePushConstants {
            float dt = 0.0f;
            uint32_t width = 0;
            uint32_t height = 0;
            uint32_t frame_index = 0;
        };

        bool create_storage_buffers();
        bool create_descriptor_resources();
        bool create_pipelines();

        void destroy_pipelines();
        void destroy_descriptors();
        void destroy_buffers();

        void update_descriptor_sets_for_state_buffers();
        VkBuffer state_buffer_handle(uint32_t index) const;

        uint32_t active_set_index() const;

        static uint32_t find_memory_type(VkPhysicalDevice phys,
                                         uint32_t type_filter,
                                         VkMemoryPropertyFlags properties);

    private:
        VulkanContext* m_context = nullptr;
        VkDevice m_device = VK_NULL_HANDLE;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;

        uint32_t m_width = 0;
        uint32_t m_height = 0;
        uint32_t m_particle_count = 0;

        BufferResource m_state_buffers[2]{};
        VkBuffer m_external_state_buffers[2]{};
        bool m_use_external_state_buffers = false;
        uint32_t m_read_index = 0;
        uint32_t m_write_index = 1;

        VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool m_descriptor_pool = VK_NULL_HANDLE;
        VkDescriptorSet m_descriptor_sets[2]{};

        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_seed_pipeline = VK_NULL_HANDLE;
        VkPipeline m_sim_pipeline = VK_NULL_HANDLE;

        bool m_initialized = false;
    };

}
