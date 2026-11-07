#include "hnpch.h"
#include "present_pass.h"

#include "pipeline.h"
#include "renderer.h"
#include "Honey/core/asset_paths.h"
#include "Honey/core/engine.h"
#include "platform/vulkan/vk_descriptor_mapping.h"
#include "platform/vulkan/vk_framebuffer.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_gpu_profiler.h"
#include "gpu_types.h"

namespace Honey {

    namespace {
        struct PresentPassResources {
            std::unordered_map<void*, Ref<Pipeline>> pipelines;
            uint32_t scene_block_offset = 0;
            uint32_t block_size = 0, block_align = 1;
        };
        static PresentPassResources* s_res = nullptr;

        Ref<Pipeline> get_or_create_pipeline(void* rp) {
            auto it = s_res->pipelines.find(rp);
            if (it != s_res->pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(AssetPaths::asset_root() / "shaders" / "Present.glsl");
            spec.cullMode = CullMode::None;
            spec.wireframe = false;
            spec.depthStencil.depthTest = false;
            spec.depthStencil.depthWrite = false;
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(1, AttachmentBlendState{});
            spec.perColorAttachmentBlend[0].enabled = false;
            spec.passType = RenderPassType::Swapchain;

            auto pipeline = Pipeline::create_heap_mode(spec, rp);

            const auto* heap = Application::get().get_vulkan_backend().get_descriptor_heap();
            std::vector<uint32_t> offsets = compute_block_offsets(spec.reflection, *heap);
            for (size_t i = 0; const auto& binding : spec.reflection.bindings) {
                if (binding.set == 1 && binding.binding == 0) {
                    s_res->scene_block_offset = offsets[i];
                    s_res->block_align = heap->descriptor_alignment(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
                    s_res->block_size = s_res->scene_block_offset + heap->descriptor_stride(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
                }
                i++;
            }

            s_res->pipelines.emplace(rp, pipeline);
            return pipeline;
        }

    }

    void PresentPass::init() {
        if (Renderer::get_api() != RendererAPI::API::vulkan) {
            HN_CORE_ERROR("Non-vulkan APIs not supported for runtime environment.");
            return;
        }

        if (s_res) return;
        s_res = new PresentPassResources{};
    }

    void PresentPass::shutdown() {
        if (!s_res) return;

        s_res->pipelines.clear();
        delete s_res;
        s_res = nullptr;
    }

    bool PresentPass::is_initialized() {
        return s_res != nullptr;
    }

    void PresentPass::record(const Ref<Framebuffer>& src, PresentFit fit) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(is_initialized(), "[PresentPass] Pass resources to not exist.");
        auto* vk = dynamic_cast<VulkanContext*>(Application::get().get_window().get_context());
        HN_CORE_ASSERT(vk->is_recording(), "[PresentPass] Cannot record presentation pass when command buffer is not recording.");

        auto fb = std::static_pointer_cast<const VulkanFramebuffer>(src);
        const VkExtent2D src_extent = fb->get_extent();
        if (src_extent.width == 0 || src_extent.height == 0) {
            HN_CORE_WARN("[PresentPass] Source framebuffer has zero size, skipping present.");
            return;
        }

        auto* heap = Application::get().get_vulkan_backend().get_descriptor_heap();
        HN_CORE_ASSERT(heap, "[PresentPass] Descriptor heap is null; present requires the Full tier.");

        VkCommandBuffer cmd = vk->get_recording_cmd();
        vk->cmd_begin_debug_label(cmd, "Present", 0.2f, 0.8f, 0.4f);
        HN_GPU_SCOPE(cmd, "Present");

        auto pipeline = get_or_create_pipeline(reinterpret_cast<void*>(vk->get_render_pass()));
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, reinterpret_cast<VkPipeline>(pipeline->get_native_pipeline()));

        // Classic binds earlier in the frame invalidate the heap binding.
        heap->bind(cmd);

        // Written every frame because a resize recreates the scene image views.
        const uint32_t stride = heap->descriptor_stride(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        const auto block = heap->allocate_transient_bytes(s_res->block_size, s_res->block_align);
        const VulkanDescriptorHeap::Allocation sub{ block.offset + s_res->scene_block_offset, stride, stride };
        heap->write_image(sub, 0, fb->get_color_view_create_info(0), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, "SceneColor", "Present");
#if defined(HN_ENABLE_ASSERTS)
        heap->debug_verify(sub.offset, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, "Present");
#endif

        PassPushData pd{};
        pd.resource_heap_base = block.offset;
        heap->push_pass_data(cmd, &pd, sizeof(pd));

        const float sw = (float)vk->get_swapchain_extent_width();
        const float sh = (float)vk->get_swapchain_extent_height();
        float x = 0.0f, y = 0.0f, w = sw, h = sh;
        if (fit == PresentFit::letterbox) {
            const float scale = std::min(sw / (float)src_extent.width, sh / (float)src_extent.height);
            w = std::floor((float)src_extent.width * scale);
            h = std::floor((float)src_extent.height * scale);
            x = std::floor((sw - w) * 0.5f);
            y = std::floor((sh - h) * 0.5f);
        }

        const VkViewport viewport{ x, y, w, h, 0.0f, 1.0f };
        const VkRect2D scissor{ { (int32_t)x, (int32_t)y }, { (uint32_t)w, (uint32_t)h } };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdDraw(cmd, 3, 1, 0, 0);

        vk->cmd_end_debug_label(cmd);
    }
}
