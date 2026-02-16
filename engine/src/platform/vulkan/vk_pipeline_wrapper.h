#pragma once

#include "vk_context.h"
#include "Honey/renderer/renderer.h"
#include "Honey/core/base.h"
#include "Honey/core/log.h"
#include "Honey/renderer/pipeline.h"

namespace Honey {
    class VulkanPipelineWrapper final : public Pipeline {
    public:
        VulkanPipelineWrapper(const PipelineSpec& spec,
                              VulkanContext* ctx,
                              VkRenderPass rp)
            : m_ctx(ctx)
        {
            HN_CORE_ASSERT(m_ctx, "VulkanPipelineWrapper: ctx is null");
            m_spec = spec;

            const auto spirv = Renderer::get_shader_cache()->get_or_compile_spirv_paths(spec.shaderGLSLPath);

            VkDevice device = m_ctx->get_device();
            VkDescriptorSetLayout global_set_layout = m_ctx->get_global_set_layout();
            VkPipelineCache pipeline_cache = m_ctx->get_pipeline_cache().get();

            m_vk.create(
                device,
                rp,
                global_set_layout,
                spirv.vertex.string(),
                spirv.fragment.string(),
                spec,
                pipeline_cache
            );
        }

        ~VulkanPipelineWrapper() override
        {
            if (!m_ctx) return;
            VkDevice device = m_ctx->get_device();
            if (device && m_vk.valid())
                m_vk.destroy(device);
        }

        void* get_native_pipeline() const override { return m_vk.pipeline(); }
        void* get_native_pipeline_layout() const override { return m_vk.layout(); }

    private:
        VulkanContext* m_ctx = nullptr;
        VulkanPipeline m_vk;
    };
}
