#include "hnpch.h"
#include "pipeline.h"

#include "renderer.h"
#include "renderer_api.h"
#include "shader_cache.h"
#include "Honey/core/log.h"

#include "platform/vulkan/vk_pipeline.h"
#include "platform/vulkan/vk_context.h"
#include "Honey/core/engine.h"

namespace Honey {

    namespace {
        class NullPipeline final : public Pipeline {
        public:
            explicit NullPipeline(const PipelineSpec& spec) { m_spec = spec; }
            void* get_native_pipeline() const override { return nullptr; }
            void* get_native_pipeline_layout() const override { return nullptr; }
        };

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

                VkDevice device = reinterpret_cast<VkDevice>(m_ctx->get_device());
                VkDescriptorSetLayout global_set_layout =
                    reinterpret_cast<VkDescriptorSetLayout>(m_ctx->get_global_set_layout());

                m_vk.create(
                    device,
                    rp,
                    global_set_layout,
                    spirv.vertex.string(),
                    spirv.fragment.string(),
                    spec
                );
            }

            ~VulkanPipelineWrapper() override
            {
                if (!m_ctx) return;
                VkDevice device = reinterpret_cast<VkDevice>(m_ctx->get_device());
                if (device && m_vk.valid())
                    m_vk.destroy(device);
            }

            void* get_native_pipeline() const override { return (void*)m_vk.pipeline(); }
            void* get_native_pipeline_layout() const override { return (void*)m_vk.layout(); }

        private:
            VulkanContext* m_ctx = nullptr;
            VulkanPipeline m_vk;
        };
    }

    Ref<Pipeline> Pipeline::create(const PipelineSpec& spec, void* native_render_pass)
    {
        switch (RendererAPI::get_api()) {
        case RendererAPI::API::vulkan: {
            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "Pipeline::create expected VulkanContext when Vulkan is active");

            VkRenderPass rp = reinterpret_cast<VkRenderPass>(native_render_pass);
            HN_CORE_ASSERT(rp, "Pipeline::create: render pass is null");

            return CreateRef<VulkanPipelineWrapper>(spec, vk, rp);
        }
        case RendererAPI::API::opengl:
        case RendererAPI::API::none:
        default:
            return CreateRef<NullPipeline>(spec);
        }
    }

}