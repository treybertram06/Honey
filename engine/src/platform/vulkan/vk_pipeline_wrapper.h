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
                              VkRenderPass rp,
                              VkDescriptorSetLayout extra_set_layout = nullptr,
                              bool heap_mode = false)
            : m_ctx(ctx)
        {
            HN_CORE_ASSERT(m_ctx, "VulkanPipelineWrapper: ctx is null");
            m_spec = spec;

            const auto spirv = Renderer::get_shader_cache()->get_or_compile_spirv_paths(spec.shaderGLSLPath);

            VkDevice device = m_ctx->get_device();
            VkDescriptorSetLayout global_set_layout = m_ctx->get_global_set_layout();
            VkPipelineCache pipeline_cache = m_ctx->get_pipeline_cache().get();

            // Heap-mode pipelines (VK_EXT_descriptor_heap) carry no layout; descriptors come from
            // the bound heaps via reflection-driven mapping. extra_set_layout is ignored.
            const VulkanDescriptorHeap* heap = heap_mode
                ? m_ctx->get_backend()->get_descriptor_heap() : nullptr;

            if (spirv.has_mesh()) {
                m_vk.create_mesh(
                    device,
                    rp,
                    global_set_layout,
                    spirv.task.string(),
                    spirv.mesh.string(),
                    spirv.fragment.string(),
                    spec,
                    pipeline_cache,
                    extra_set_layout,
                    heap,
                    heap_mode
                );
            } else {
                m_vk.create(
                    device,
                    rp,
                    global_set_layout,
                    spirv.vertex.string(),
                    spirv.fragment.string(),
                    spec,
                    pipeline_cache,
                    extra_set_layout,
                    heap,
                    heap_mode
                );
            }
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
        bool  is_heap_mode() const override { return m_vk.heap_mode(); }

    private:
        VulkanContext* m_ctx = nullptr;
        VulkanPipeline m_vk;
    };
}
