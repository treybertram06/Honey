#include "hnpch.h"
#include "vk_one_shot_compute_pass.h"

#include "vk_backend.h"
#include "Honey/renderer/renderer.h"

#include <algorithm>

namespace Honey {

    namespace {
        VkDescriptorPool create_pool(VkDevice device, const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
            // One set's worth of each descriptor type used by `bindings`, counted by type since a
            // pass may bind e.g. two storage images and a sampler. maxSets=1: record() only ever
            // has one descriptor set live at a time (see header) and resets the pool before every
            // allocation, so this never needs to grow.
            std::vector<VkDescriptorPoolSize> sizes;
            for (const auto& b : bindings) {
                auto it = std::find_if(sizes.begin(), sizes.end(), [&](const VkDescriptorPoolSize& s) {
                    return s.type == b.descriptorType;
                });
                if (it != sizes.end())
                    it->descriptorCount += b.descriptorCount;
                else
                    sizes.push_back({ b.descriptorType, b.descriptorCount });
            }

            VkDescriptorPoolCreateInfo pool_ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pool_ci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_ci.maxSets = 1;
            pool_ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
            pool_ci.pPoolSizes = sizes.data();

            VkDescriptorPool pool = VK_NULL_HANDLE;
            VkResult r = vkCreateDescriptorPool(device, &pool_ci, nullptr, &pool);
            HN_CORE_ASSERT(r == VK_SUCCESS, "OneShotComputePass: failed to create descriptor pool");
            return pool;
        }
    }

    OneShotComputePass::OneShotComputePass(VulkanBackend* backend,
        const std::filesystem::path& shader_path,
        std::vector<VkDescriptorSetLayoutBinding> bindings,
        uint32_t push_constant_size)
        : m_backend(backend), m_device(backend->device()) {
        HN_PROFILE_FUNCTION();

        for (auto& b : bindings)
            b.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layout_ci{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        layout_ci.bindingCount = static_cast<uint32_t>(bindings.size());
        layout_ci.pBindings = bindings.data();
        VkResult r = vkCreateDescriptorSetLayout(m_device, &layout_ci, nullptr, &m_set_layout);
        HN_CORE_ASSERT(r == VK_SUCCESS, "OneShotComputePass: failed to create descriptor set layout for '{0}'",
                       shader_path.string());

        m_pool = create_pool(m_device, bindings);

        VkPipelineLayoutCreateInfo pl_ci{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        pl_ci.setLayoutCount = 1;
        pl_ci.pSetLayouts = &m_set_layout;

        VkPushConstantRange pc_range{};
        if (push_constant_size > 0) {
            pc_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pc_range.offset = 0;
            pc_range.size = push_constant_size;
            pl_ci.pushConstantRangeCount = 1;
            pl_ci.pPushConstantRanges = &pc_range;
        }

        r = vkCreatePipelineLayout(m_device, &pl_ci, nullptr, &m_pipeline_layout);
        HN_CORE_ASSERT(r == VK_SUCCESS, "OneShotComputePass: failed to create pipeline layout for '{0}'",
                       shader_path.string());

        auto spirv = Renderer::get_shader_cache()->get_or_compile_stage_spirv(shader_path);
        HN_CORE_ASSERT(!spirv.empty(), "OneShotComputePass: SPIR-V compilation failed for '{0}'", shader_path.string());

        VkShaderModuleCreateInfo sm_ci{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
        sm_ci.codeSize = spirv.size() * sizeof(uint32_t);
        sm_ci.pCode = spirv.data();
        VkShaderModule shader_module = VK_NULL_HANDLE;
        r = vkCreateShaderModule(m_device, &sm_ci, nullptr, &shader_module);
        HN_CORE_ASSERT(r == VK_SUCCESS, "OneShotComputePass: failed to create shader module for '{0}'",
                       shader_path.string());

        VkComputePipelineCreateInfo cp_ci{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
        cp_ci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cp_ci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        cp_ci.stage.module = shader_module;
        cp_ci.stage.pName = "main";
        cp_ci.layout = m_pipeline_layout;

        r = vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &cp_ci, nullptr, &m_pipeline);
        vkDestroyShaderModule(m_device, shader_module, nullptr);
        HN_CORE_ASSERT(r == VK_SUCCESS, "OneShotComputePass: failed to create compute pipeline for '{0}'",
                       shader_path.string());

        HN_CORE_INFO("[OneShotComputePass] Built compute pipeline for '{0}'", shader_path.string());
    }

    OneShotComputePass::~OneShotComputePass() {
        if (!m_device) return;
        if (m_pipeline) vkDestroyPipeline(m_device, m_pipeline, nullptr);
        if (m_pipeline_layout) vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
        if (m_pool) vkDestroyDescriptorPool(m_device, m_pool, nullptr);
        if (m_set_layout) vkDestroyDescriptorSetLayout(m_device, m_set_layout, nullptr);
    }

    void OneShotComputePass::record(VkCommandBuffer cmd,
        std::vector<VkWriteDescriptorSet> writes,
        const void* push_data, uint32_t push_size,
        uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
        HN_PROFILE_FUNCTION();

        // Reset rather than free/reallocate: record() only ever runs inside a caller's
        // immediate_submit, which blocks until the GPU is idle before returning, so the previous
        // descriptor set is guaranteed unused by the time record() is called again.
        vkResetDescriptorPool(m_device, m_pool, 0);

        VkDescriptorSetAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        alloc_info.descriptorPool = m_pool;
        alloc_info.descriptorSetCount = 1;
        alloc_info.pSetLayouts = &m_set_layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VkResult r = vkAllocateDescriptorSets(m_device, &alloc_info, &set);
        HN_CORE_ASSERT(r == VK_SUCCESS, "OneShotComputePass::record: failed to allocate descriptor set");

        for (auto& w : writes) {
            w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet = set;
        }
        vkUpdateDescriptorSets(m_device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline_layout, 0, 1, &set, 0, nullptr);
        if (push_data && push_size > 0)
            vkCmdPushConstants(cmd, m_pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, push_size, push_data);

        vkCmdDispatch(cmd, group_count_x, group_count_y, group_count_z);
    }

}