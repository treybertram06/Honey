#include "hnpch.h"
#include "renderer_3d_vector_icon.h"

#include "renderer_3d_internal.h"
#include "Honey/core/engine.h"
#include "Honey/renderer/frame_graph_registry.h"
#include "Honey/renderer/pipeline.h"
#include "Honey/renderer/renderer.h"
#include "platform/vulkan/vk_buffer.h"
#include "platform/vulkan/vk_context.h"
#include "platform/vulkan/vk_framebuffer.h"

namespace Honey {

    static const std::filesystem::path asset_root = ASSET_ROOT;

    namespace {

        struct VectorIconResources {
            VulkanContext* vk_ctx = nullptr;

            std::unordered_map<void*, Ref<Pipeline>> vector_icon_pipelines;
        };
        static VectorIconResources* s_res = nullptr;

        Ref<Pipeline> get_or_create_vector_pipeline(void* rp_native) {
            auto it = s_res->vector_icon_pipelines.find(rp_native);
            if (it != s_res->vector_icon_pipelines.end())
                return it->second;

            auto spec = PipelineSpec::from_shader(asset_root / "shaders" / "Renderer3D_VectorIcon.glsl");
            spec.depthStencil.depthTest  = false; // Should be occluded by geometry - handled in the fragment shader
            spec.depthStencil.depthWrite = false; // Should not occlude geometry
            spec.perColorAttachmentBlend.clear();
            spec.perColorAttachmentBlend.resize(2, AttachmentBlendState{});
            spec.perColorAttachmentBlend[0].enabled = true; // vectorTexture: alpha blend for curve-edge AA
            // vectorEntityTexture (attachment 1) stays disabled - it's an integer entity-id target,
            // and Vulkan requires blendEnable=false for integer attachment formats anyway.
            spec.topology = PrimitiveTopology::TriangleStrip;

            auto pipeline = Pipeline::create_heap_mode(spec, rp_native);
            s_res->vector_icon_pipelines.emplace(rp_native, pipeline);
            return pipeline;
        }
    }

    void Renderer3DVectorIcon::init(VulkanContext* ctx) {
        if (s_res) return;
        s_res = new VectorIconResources{};
        s_res->vk_ctx = ctx;
    }

    void Renderer3DVectorIcon::shutdown() {
        if (!s_res) return;
        delete s_res;
        s_res = nullptr;
    }

    void Renderer3DVectorIcon::execute_draw(FrameGraphPassContext& ctx) {
        HN_PROFILE_FUNCTION();
        if (!s_res || !s_res->vk_ctx || !Renderer3DInternal::g_renderer3d_data) return;

        auto* data    = Renderer3DInternal::g_renderer3d_data;
        if (data->icon_draws.empty()) return;

        auto  target  = ctx.get_pass_target_framebuffer();
        auto* vk_fb   = dynamic_cast<VulkanFramebuffer*>(target.get());
        HN_CORE_ASSERT(vk_fb, "execute_draw: target is not a VulkanFramebuffer");
        void* rp_native = vk_fb->get_render_pass();

        Ref<Pipeline> pipe = get_or_create_vector_pipeline(rp_native);
        VkPipeline vk_pipe = reinterpret_cast<VkPipeline>(pipe->get_native_pipeline());
        HN_CORE_ASSERT(vk_pipe, "execute_draw: heap-mode pipeline is null");

        // One icon submission fans out into one GPUIconInstance per SlugIcon *shape* - a multi-shape
        // SVG (e.g. a lightbulb with separate glass/filament/base fills) needs each shape's own
        // bbox/band range/fill color, not the icon's combined slot (see VectorIcon::get_shapes()).
        std::vector<GPUIconInstance> draw_data;
        for (auto& icon_draw : data->icon_draws) {
            const auto& icon = icon_draw.icon;
            const glm::vec2 canvas_size{ icon->get_width(), icon->get_height() };
            for (const auto& shape : icon->get_shapes()) {
                draw_data.push_back(GPUIconInstance{
                    .world_pos = icon_draw.world_pos,
                    .tint = icon_draw.tint,
                    .fill_color = shape.fill_color,
                    .bbox = glm::vec4(shape.bbox_min, shape.bbox_max),

                    .canvas_size = canvas_size,
                    .entity_id = icon_draw.entity_id,
                    .sizemode = (uint32_t)icon_draw.sizemode,

                    .band_offset = shape.band_table_offset,
                    .band_count = shape.num_bands,
                });
            }
        }

        const uint32_t total_draws = (uint32_t)draw_data.size();
        const uint32_t instance_bytes = total_draws * sizeof(GPUIconInstance);

        const uint32_t frame_slot = s_res->vk_ctx->get_current_frame() % VulkanContext::k_max_frames_in_flight;
        auto& instance_buffer = Renderer3DInternal::g_renderer3d_data->icon_instance_buffers[frame_slot];

        if (!instance_buffer || instance_buffer->get_size() < instance_bytes)
            instance_buffer = StorageBuffer::create(instance_bytes, StorageBufferUsage::Dynamic);

        instance_buffer->set_data(draw_data.data(), instance_bytes);

        auto* vk_instance_buf = static_cast<VulkanStorageBuffer*>(instance_buffer.get());
        const VkDeviceAddress instance_addr = vk_instance_buf->device_address();

        const VkExtent2D ext = s_res->vk_ctx->get_current_pass_extent();
        VkViewport vp{ 0, 0, (float)ext.width, (float)ext.height, 0.0f, 1.0f };
        VkRect2D sc{ { 0, 0 }, { ext.width, ext.height } };

        ctx.submit_vulkan_graphics_raw([&](VkCommandBuffer cmd) {
            HN_GPU_SCOPE(cmd, "Vector Icon Draw");
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipe);
            vkCmdSetViewport(cmd, 0, 1, &vp);
            vkCmdSetScissor(cmd, 0, 1, &sc);
            ctx.bind_heap_pipeline(*pipe);

            // IconInstances (set=1, binding=2) has no matching .hnfg read binding by design - it's an
            // ad-hoc per-frame CPU-built SSBO, not a frame-graph-tracked resource - so bind_heap_pipeline
            // reserves the slot (via shader reflection) but leaves it unwritten. Hand-write it at the
            // exact offset reflection assigned it, not a guessed index into the whole pass block.
            auto* heap = s_res->vk_ctx->get_backend()->get_descriptor_heap();
            auto pass_alloc = ctx.get_pass_descriptor_allocation();
            const uint32_t binding_offset = ctx.get_pass_binding_block_offset(1, 2);
            const uint32_t stride = heap->descriptor_stride(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            VulkanDescriptorHeap::Allocation heap_alloc{ pass_alloc.offset + binding_offset, stride, stride };
            heap->write_buffer(heap_alloc, 0, instance_addr, instance_bytes, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

            vkCmdDraw(cmd, 4, total_draws, 0, 0);
        });
    }

    void Renderer3DVectorIcon::register_frame_graph_executors() {
        auto& registry = FrameGraphRegistry::get();
        registry.register_executor("vector.draw", [](FrameGraphPassContext& ctx) {
            execute_draw(ctx);
        });
    }

    bool Renderer3DVectorIcon::is_initialized() {
        return s_res != nullptr;
    }
}
