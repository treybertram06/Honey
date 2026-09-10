#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include <vulkan/vulkan.h>

namespace Honey {

    class VulkanBackend;

    // A cached, reusable compute pipeline for infrequent, blocking GPU work -- asset bakes and
    // format conversions (equirect->cube, IBL irradiance/prefilter convolution, LUT bakes), never
    // a per-frame render pass. Builds its descriptor set layout, pipeline layout, and pipeline
    // once from a .comp shader, then can be record()-ed repeatedly with different descriptor
    // writes / push-constant data (e.g. once per mip level of a prefiltered env map).
    //
    // This deliberately does NOT own barriers: every real caller needs different image-layout
    // transitions around its dispatch (different resources, different subresource ranges), so
    // barrier recording stays the caller's job, inside the same command buffer as record().
    class OneShotComputePass {
    public:
        // `bindings` describes descriptor set 0 only -- one-shot passes never need more than one
        // set. stageFlags on each binding is overwritten with VK_SHADER_STAGE_COMPUTE_BIT; this
        // pass is compute-only by construction, so callers don't need to fill that in.
        // `push_constant_size` may be 0 if the shader takes no push constants.
        OneShotComputePass(VulkanBackend* backend,
                            const std::filesystem::path& shader_path,
                            std::vector<VkDescriptorSetLayoutBinding> bindings,
                            uint32_t push_constant_size = 0);
        ~OneShotComputePass();

        OneShotComputePass(const OneShotComputePass&) = delete;
        OneShotComputePass& operator=(const OneShotComputePass&) = delete;

        // Allocates descriptor set 0, writes `writes` into it (leave dstSet/sType default in each
        // entry -- both are filled in here), then binds pipeline + set + push constant and
        // dispatches. Must be called from inside a command buffer submitted via
        // VulkanBackend::immediate_submit(): the descriptor pool is reset on every call, which is
        // only safe because immediate_submit blocks until the GPU is idle before returning. Never
        // call record() twice against the same instance inside one command buffer -- the second
        // reset would invalidate the first call's still-pending descriptor set.
        void record(VkCommandBuffer cmd,
                    std::vector<VkWriteDescriptorSet> writes,
                    const void* push_data, uint32_t push_size,
                    uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z);

    private:
        VulkanBackend* m_backend = nullptr;
        VkDevice m_device = VK_NULL_HANDLE;

        VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
        VkDescriptorPool m_pool = VK_NULL_HANDLE;
        VkPipelineLayout m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;
    };

}