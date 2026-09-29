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
                              VkPipelineLayout layout = VK_NULL_HANDLE) // non-null => layout mode (lite tier)
            : m_ctx(ctx), m_layout_mode(layout != VK_NULL_HANDLE)
        {
            HN_CORE_ASSERT(m_ctx, "VulkanPipelineWrapper: ctx is null");
            m_spec = spec;

            const auto spirv = Renderer::get_shader_cache()->get_or_compile_spirv_paths(spec.shaderGLSLPath);

            VkDevice device = m_ctx->get_device();
            VkPipelineCache pipeline_cache = m_ctx->get_pipeline_cache().get();

            // Heap-mode pipelines (VK_EXT_descriptor_heap) carry no layout; descriptors come from
            // the bound heaps via reflection-driven mapping. extra_set_layout is ignored.
            const VulkanDescriptorHeap* heap = m_ctx->get_backend()->get_descriptor_heap();

            if (spirv.has_mesh()) {
                HN_CORE_ASSERT(!m_layout_mode, "VulkanPipelineWrapper: layout mode does not support mesh pipelines");
                m_vk.create_mesh(
                    device,
                    rp,
                    spirv.task.string(),
                    spirv.mesh.string(),
                    spirv.fragment.string(),
                    spec,
                    pipeline_cache,
                    heap
                );
            } else {
                m_vk.create(
                    device,
                    rp,
                    spirv.vertex.string(),
                    spirv.fragment.string(),
                    spec,
                    pipeline_cache,
                    heap,
                    layout
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
        bool is_heap_mode() const override { return !m_layout_mode; }

    private:
        VulkanContext* m_ctx = nullptr;
        bool m_layout_mode = false;
        VulkanPipeline m_vk;
    };
}
