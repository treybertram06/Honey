#pragma once

#include <string>
#include "Honey/renderer/pipeline_spec.h"

typedef struct VkDevice_T* VkDevice;
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkDescriptorSetLayout_T* VkDescriptorSetLayout;
typedef struct VkShaderModule_T* VkShaderModule;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkPipelineCache_T* VkPipelineCache;

namespace Honey {

    class VulkanPipeline {
    public:
        VulkanPipeline() = default;
        ~VulkanPipeline() = default;

        void create(
            VkDevice device,
            VkRenderPass renderPass,
            VkDescriptorSetLayout globalSetLayout,
            const std::string& vertexSpirvPath,
            const std::string& fragmentSpirvPath,
            const PipelineSpec& spec,
            VkPipelineCache pipelineCache = nullptr
        );

        void destroy(VkDevice device);

        bool valid() const { return m_pipeline != nullptr && m_layout != nullptr; }
        void* pipeline() const { return m_pipeline; }
        void* layout() const { return m_layout; }

        // Engine contract: we allow pushing arbitrary data up to 128 bytes.
        // Vulkan guarantees at least 128 bytes via VkPhysicalDeviceLimits::maxPushConstantsSize.
        static constexpr uint32_t k_push_constant_max_size = 128;

    private:
        static VkShaderModule create_shader_module_from_file(VkDevice device, const std::string& path);

        void* m_pipeline = nullptr;     // VkPipeline
        void* m_layout   = nullptr;     // VkPipelineLayout
        void* m_vert_module = nullptr;  // VkShaderModule
        void* m_frag_module = nullptr;  // VkShaderModule
    };

} // namespace Honey