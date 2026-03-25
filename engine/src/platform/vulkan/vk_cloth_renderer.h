#pragma once

#include "vk_context.h"

#include <glm/glm.hpp>
#include <unordered_map>

namespace Honey {

    // Renders a cloth particle grid from a GPU storage buffer.
    // Uses pure SSBO vertex-pulling (no CPU-side mesh): the vertex shader reads
    // particle positions directly from the compute output buffer.
    class VulkanClothRenderer {
    public:
        VulkanClothRenderer() = default;
        ~VulkanClothRenderer();

        bool init(VulkanContext* context, uint32_t width, uint32_t height);
        void shutdown();

        bool valid() const { return m_initialized; }

        // Must be called inside a frame-graph graphics pass (render pass is open).
        // Queues a CustomVulkan command into the FramePacket that records cloth draw
        // commands into the command buffer during record_command_buffer().
        //
        // cloth_state_buf: VkBuffer holding the current cloth particle state (read-only).
        // vp_engine_clip:  View-projection matrix in EngineClip (GL-style Y-up, Z in [-1,1]).
        //                  Vulkan clip-space correction is applied internally.
        void record_draw(VkBuffer cloth_state_buf, const glm::mat4& vp_engine_clip);

    private:
        struct PipelineEntry {
            VkPipeline       pipeline = VK_NULL_HANDLE;
            VkPipelineLayout layout   = VK_NULL_HANDLE;
        };

        bool ensure_pipeline_for_render_pass(VkRenderPass rp);
        void destroy_pipelines();
        void destroy_descriptor_resources();
        void destroy_index_buffer();

        static uint32_t find_memory_type(VkPhysicalDevice phys,
                                         uint32_t type_filter,
                                         VkMemoryPropertyFlags properties);

        VulkanContext*   m_context         = nullptr;
        VkDevice         m_device          = VK_NULL_HANDLE;
        VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;

        uint32_t m_width  = 0;
        uint32_t m_height = 0;

        // Descriptor resources — one set per frame-in-flight
        VkDescriptorSetLayout m_descriptor_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool      m_descriptor_pool       = VK_NULL_HANDLE;
        VkDescriptorSet  m_descriptor_sets[VulkanContext::k_max_frames_in_flight]{};
        VkBuffer         m_last_bound_buffers[VulkanContext::k_max_frames_in_flight]{};

        // Per-render-pass pipelines (keyed on VkRenderPass pointer)
        std::unordered_map<VkRenderPass, PipelineEntry> m_pipelines;

        // Static index buffer encoding the grid triangle topology
        VkBuffer       m_index_buffer = VK_NULL_HANDLE;
        VkDeviceMemory m_index_memory = VK_NULL_HANDLE;
        uint32_t       m_index_count  = 0;

        bool m_initialized = false;
    };

} // namespace Honey
