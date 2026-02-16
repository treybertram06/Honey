#include "hnpch.h"
#include "pipeline.h"

#include "renderer.h"
#include "renderer_api.h"
#include "shader_cache.h"
#include "Honey/core/log.h"

#include "platform/vulkan/vk_pipeline.h"
#include "platform/vulkan/vk_context.h"
#include "Honey/core/engine.h"
#include "platform/vulkan/vk_pipeline_wrapper.h"

namespace Honey {

    namespace {
        class NullPipeline final : public Pipeline {
        public:
            explicit NullPipeline(const PipelineSpec& spec = PipelineSpec{}) { m_spec = spec; }
            void* get_native_pipeline() const override { return nullptr; }
            void* get_native_pipeline_layout() const override { return nullptr; }
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

    Ref<Pipeline> Pipeline::create(const std::filesystem::path& path, void* native_render_pass) {
        switch (RendererAPI::get_api()) {
        case RendererAPI::API::vulkan: {
                auto* base = Application::get().get_window().get_context();
                auto* vk = dynamic_cast<VulkanContext*>(base);
                HN_CORE_ASSERT(vk, "Pipeline::create expected VulkanContext when Vulkan is active");

                VkRenderPass rp = static_cast<VkRenderPass>(native_render_pass);
                HN_CORE_ASSERT(rp, "Pipeline::create: render pass is null");

                // build spec from shader with spirv-reflect
                auto spec = PipelineSpec::from_shader(path);

                return CreateRef<VulkanPipelineWrapper>(spec, vk, rp);
        }
        case RendererAPI::API::opengl:
        case RendererAPI::API::none:
        default:
            return CreateRef<NullPipeline>(); // Uses empty spec since it doesn't get used anyway (I think?)
        }
    }
}
