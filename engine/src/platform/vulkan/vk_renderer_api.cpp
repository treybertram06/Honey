#include "hnpch.h"
#include "vk_renderer_api.h"
#include "vk_context.h"

namespace Honey {
    void VulkanRendererAPI::init() {
        HN_PROFILE_FUNCTION();

        // Vulkan rendering state is configured per-pipeline and command buffer
        // rather than as global state like OpenGL
    }

    void VulkanRendererAPI::set_clear_color(const glm::vec4 &color) {
        m_clear_color = color;
    }

    void VulkanRendererAPI::set_viewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
        // Store viewport for use during command buffer recording
        m_viewport_x = x;
        m_viewport_y = y;
        m_viewport_width = width;
        m_viewport_height = height;
    }

    void VulkanRendererAPI::clear() {
        // Clear operations in Vulkan are done via render pass load operations
        // or vkCmdClearAttachments during command buffer recording
        // This will be implemented when command buffer recording is set up
        HN_CORE_WARN("VulkanRendererAPI::clear() - Not yet fully implemented. Clear is handled via render passes.");
    }

    uint32_t VulkanRendererAPI::get_max_texture_slots() {
        // Query device limits for max descriptor set samplers
        VkPhysicalDevice physical_device = VulkanContext::get_physical_device();
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(physical_device, &properties);

        // Return the maximum number of sampler descriptors per stage
        return properties.limits.maxPerStageDescriptorSamplers;
    }

    void VulkanRendererAPI::draw_indexed(const Ref<VertexArray> &vertex_array, uint32_t index_count) {
        // Draw indexed requires active command buffer, pipeline, and bound vertex/index buffers
        // This will be implemented when the full rendering pipeline is set up
        HN_CORE_WARN("VulkanRendererAPI::draw_indexed() - Not yet fully implemented. Requires command buffer and pipeline setup.");
    }

    void VulkanRendererAPI::draw_indexed_instanced(const Ref<VertexArray> &vertex_array, uint32_t index_count,
            uint32_t instance_count) {
        // Draw indexed instanced requires active command buffer, pipeline, and bound vertex/index buffers
        // This will be implemented when the full rendering pipeline is set up
        HN_CORE_WARN("VulkanRendererAPI::draw_indexed_instanced() - Not yet fully implemented. Requires command buffer and pipeline setup.");
    }

    void VulkanRendererAPI::set_wireframe(bool mode) {
        // Wireframe mode is set in pipeline rasterization state, not as dynamic state
        m_wireframe_mode = mode;
        HN_CORE_WARN("VulkanRendererAPI::set_wireframe() - Wireframe mode should be set during pipeline creation.");
    }

    void VulkanRendererAPI::set_depth_test(bool mode) {
        // Depth testing is configured in pipeline depth stencil state
        m_depth_test_enabled = mode;
        HN_CORE_WARN("VulkanRendererAPI::set_depth_test() - Depth test should be configured during pipeline creation.");
    }

    void VulkanRendererAPI::set_blend(bool mode) {
        // Blending is configured in pipeline color blend state
        m_blend_enabled = mode;
        HN_CORE_WARN("VulkanRendererAPI::set_blend() - Blend mode should be configured during pipeline creation.");
    }

    void VulkanRendererAPI::set_blend_for_attachment(uint32_t attachment, bool mode) {
        // Per-attachment blending is configured in pipeline color blend state
        if (m_blend_per_attachment.size() <= attachment) {
            m_blend_per_attachment.resize(attachment + 1, false);
        }
        m_blend_per_attachment[attachment] = mode;
        HN_CORE_WARN("VulkanRendererAPI::set_blend_for_attachment() - Per-attachment blend should be configured during pipeline creation.");
    }
}
