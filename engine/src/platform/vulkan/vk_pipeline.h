#pragma once

#include <string>
#include "Honey/renderer/pipeline_spec.h"

#include "vk_types.h"

namespace Honey {

    class VulkanDescriptorHeap; // heap-mode (VK_EXT_descriptor_heap) pipelines, used only in the .cpp

    class VulkanPipeline {
    public:
        VulkanPipeline() = default;
        ~VulkanPipeline() = default;

        void create(
            VkDevice device,
            VkRenderPass render_pass,
            VkDescriptorSetLayout global_set_layout,
            const std::string& vertex_spirv_path,
            const std::string& fragment_spirv_path,
            const PipelineSpec& spec,
            VkPipelineCache pipeline_cache  = nullptr,
            VkDescriptorSetLayout extra_set_layout = nullptr, // optional set 1 layout (e.g. font SSBOs)
            const VulkanDescriptorHeap* heap = nullptr,       // required iff heap_mode
            bool heap_mode = false                            // true → null layout + descriptor-heap mapping
        );

        void create_mesh(
            VkDevice device,
            VkRenderPass render_pass,
            VkDescriptorSetLayout global_set_layout,
            const std::string& task_spirv_path,
            const std::string& mesh_spirv_path,
            const std::string& fragment_spirv_path,
            const PipelineSpec& spec,
            VkPipelineCache pipeline_cache = nullptr,
            VkDescriptorSetLayout extra_set_layout = nullptr, // optional set 1 layout (e.g. font SSBOs)
            const VulkanDescriptorHeap* heap = nullptr,       // required iff heap_mode
            bool heap_mode = false                            // true → null layout + descriptor-heap mapping
            );

        void destroy(VkDevice device);

        // Heap-mode pipelines carry no layout, so a null m_layout is valid when m_heap_mode.
        bool valid() const { return m_pipeline != nullptr && (m_heap_mode || m_layout != nullptr); }
        void* pipeline() const { return m_pipeline; }
        void* layout() const { return m_layout; }

        // Engine contract: we allow pushing arbitrary data up to 128 bytes.
        // Vulkan guarantees at least 128 bytes via VkPhysicalDeviceLimits::maxPushConstantsSize.
        static constexpr uint32_t k_push_constant_max_size = 128;

    private:
        static VkShaderModule create_shader_module_from_file(VkDevice device, const std::string& path);

        void* m_pipeline = nullptr;     // VkPipeline
        void* m_layout   = nullptr;     // VkPipelineLayout (null in heap mode)
        bool  m_heap_mode = false;      // built with VK_EXT_descriptor_heap mapping, no layout
        void* m_vert_module = nullptr;  // VkShaderModule
        void* m_frag_module = nullptr;  // VkShaderModule
        void* m_task_module = nullptr;  // VkShaderModule
        void* m_mesh_module = nullptr;  // VkShaderModule
    };

} // namespace Honey