#include "hnpch.h"
#include "vk_renderer_api.h"

#include "Honey/renderer/pipeline.h"
#include "vk_buffer.h"
#include "vk_framebuffer.h"
#include "vk_vertex_array.h"
#include "Honey/core/engine.h"

namespace Honey {

    static thread_local VulkanContext* s_recording_context = nullptr;

    static VulkanContext* get_vulkan_context() {
        if (s_recording_context)
            return s_recording_context;
        auto* base = Application::get().get_window().get_context();
        return dynamic_cast<VulkanContext*>(base);
    }

    static void require_frame_begun() {
        HN_CORE_ASSERT(s_recording_context && s_recording_context->is_recording(),
            "VulkanRendererAPI: frame not begun. Call Renderer::begin_frame() before submitting any Vulkan render commands.");
    }

    void VulkanRendererAPI::set_recording_context(VulkanContext* ctx) {
        s_recording_context = ctx;
    }

    void VulkanRendererAPI::init() {
        HN_PROFILE_FUNCTION();
        HN_CORE_INFO("VulkanRendererAPI::init");

        auto* ctx = Application::get().get_window().get_context();
        auto* vk = dynamic_cast<VulkanContext*>(ctx);
        HN_CORE_ASSERT(vk, "Expected VulkanContext when VulkanRendererAPI is active");

        m_device = vk->get_device();
        m_physical_device = vk->get_physical_device();
    }

    uint32_t VulkanRendererAPI::get_max_texture_slots() {
        return k_max_texture_slots;
    }

    void VulkanRendererAPI::bind_pipeline(const Ref<Pipeline>& pipeline) {
        require_frame_begun();
        HN_CORE_ASSERT(pipeline, "VulkanRendererAPI::bind_pipeline: pipeline is null");

        auto* ctx = s_recording_context;
        VkCommandBuffer cmd = ctx->get_recording_cmd();

        VkPipeline    vk_pipeline = reinterpret_cast<VkPipeline>(pipeline->get_native_pipeline());
        VkPipelineLayout layout   = reinterpret_cast<VkPipelineLayout>(pipeline->get_native_pipeline_layout());

        HN_CORE_ASSERT(vk_pipeline, "VulkanRendererAPI::bind_pipeline: native VkPipeline is null");
        HN_CORE_ASSERT(layout,      "VulkanRendererAPI::bind_pipeline: native VkPipelineLayout is null");

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vk_pipeline);

        const VkExtent2D ext = ctx->get_current_pass_extent();
        VkViewport viewport{};
        viewport.x = 0.0f; viewport.y = 0.0f;
        viewport.width  = static_cast<float>(ext.width);
        viewport.height = static_cast<float>(ext.height);
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { ext.width, ext.height };
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        ctx->set_current_pipeline_layout(layout);
    }

    void VulkanRendererAPI::set_clear_color(const glm::vec4& color) {
        require_frame_begun();
        s_recording_context->set_clear_color(color);
    }

    void VulkanRendererAPI::set_viewport(uint32_t, uint32_t, uint32_t, uint32_t) {
        // handled by swapchain extent for now
    }

    void VulkanRendererAPI::clear() {
        require_frame_begun();
        // no-op for now
    }

    std::string VulkanRendererAPI::get_vendor() {
        if (!m_physical_device) {
            HN_CORE_WARN("VulkanRendererAPI::get_vendor called before physical device was set");
            return "Unknown (no device)";
        }

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_physical_device, &props);

        // props.deviceName is a null-terminated C string.
        // Example values: "NVIDIA GeForce RTX 3080", "AMD Radeon RX 6800", etc.
        return std::string(props.deviceName ? props.deviceName : "Unknown");
    }

    static void do_draw_indexed(
        VkCommandBuffer cmd,
        const Ref<VertexArray>& va,
        uint32_t index_count,
        uint32_t instance_count,
        const Ref<VertexBuffer>* extra_vbs,
        const uint32_t* extra_vb_offsets,
        uint32_t extra_vb_count)
    {
        HN_CORE_ASSERT(va, "Vulkan draw: vertex_array is null");
        const auto& ib = va->get_index_buffer();
        HN_CORE_ASSERT(ib, "Vulkan draw: VertexArray has no index buffer");

        static constexpr uint32_t kMax = 4;
        const auto& base_vbs = va->get_vertex_buffers();
        const uint32_t total = static_cast<uint32_t>(base_vbs.size()) + extra_vb_count;
        HN_CORE_ASSERT(total <= kMax, "Vulkan draw: too many vertex buffers");

        VkBuffer     buffers[kMax]{};
        VkDeviceSize offsets[kMax]{};
        for (uint32_t i = 0; i < (uint32_t)base_vbs.size(); ++i) {
            buffers[i] = reinterpret_cast<VkBuffer>(base_vbs[i]->get_native_buffer());
            offsets[i] = 0;
        }
        for (uint32_t i = 0; i < extra_vb_count; ++i) {
            const uint32_t idx = static_cast<uint32_t>(base_vbs.size()) + i;
            buffers[idx] = reinterpret_cast<VkBuffer>(extra_vbs[i]->get_native_buffer());
            offsets[idx] = static_cast<VkDeviceSize>(extra_vb_offsets[i]);
        }
        vkCmdBindVertexBuffers(cmd, 0, total, buffers, offsets);

        auto vk_ib = std::dynamic_pointer_cast<VulkanIndexBuffer>(ib);
        HN_CORE_ASSERT(vk_ib, "Vulkan draw: expected VulkanIndexBuffer");
        vkCmdBindIndexBuffer(cmd, reinterpret_cast<VkBuffer>(vk_ib->get_vk_buffer()), 0, vk_ib->get_type());

        const uint32_t ic = (index_count != 0) ? index_count : vk_ib->get_count();
        vkCmdDrawIndexed(cmd, ic, instance_count ? instance_count : 1, 0, 0, 0);
    }

    void VulkanRendererAPI::draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count) {
        require_frame_begun();
        do_draw_indexed(s_recording_context->get_recording_cmd(),
                        vertex_array, index_count, 1, nullptr, nullptr, 0);
    }

    void VulkanRendererAPI::draw_indexed_instanced(const Ref<VertexArray>& vertex_array, uint32_t index_count, uint32_t instance_count) {
        require_frame_begun();
        HN_CORE_ASSERT(instance_count > 0, "Vulkan draw_indexed_instanced: instance_count must be > 0");
        do_draw_indexed(s_recording_context->get_recording_cmd(),
                        vertex_array, index_count, instance_count, nullptr, nullptr, 0);
    }

    void VulkanRendererAPI::submit_mesh_tasks_draw(uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z) {
        require_frame_begun();

        auto* ctx = get_vulkan_context();
        HN_CORE_ASSERT(ctx, "submit_mesh_tasks_draw: no active VulkanContext");

        static PFN_vkCmdDrawMeshTasksEXT fn = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(
            vkGetDeviceProcAddr(ctx->get_device(), "vkCmdDrawMeshTasksEXT"));
        HN_CORE_ASSERT(fn, "submit_mesh_tasks_draw: vkCmdDrawMeshTasksEXT not available — is VK_EXT_mesh_shader enabled?");

        ctx->queue_custom_vulkan_cmd([group_count_x, group_count_y, group_count_z]
            (VkCommandBuffer cmd, uint32_t, uint32_t)
        {
            fn(cmd, group_count_x, group_count_y, group_count_z);
        });
    }

    void VulkanRendererAPI::update_mesh_draw_data_binding(GlobalMeshletBuffers& bufs, uint32_t draw_count) {
        const uint32_t needed = draw_count * (uint32_t)sizeof(GPUDrawData);
        const uint32_t slot = get_meshlet_frame_slot();
        auto& draw_data_buffer = bufs.draw_data_buffers[slot];
        if (!draw_data_buffer || draw_data_buffer->get_size() < needed) {
            draw_data_buffer = StorageBuffer::create(needed, StorageBufferUsage::Dynamic);

            HN_CORE_ASSERT(bufs.meshlet_blocks[slot].valid,
                "update_mesh_draw_data_binding: meshlet heap block not allocated — "
                "call allocate_meshlet_heap_blocks on mesh load first");

            auto* ctx = get_vulkan_context();
            auto* heap = ctx->get_backend()->get_descriptor_heap();
            const auto& block = bufs.meshlet_blocks[slot];
            VulkanDescriptorHeap::Allocation alloc{block.offset, block.size, block.stride};

            auto* vk_buf = static_cast<VulkanStorageBuffer*>(draw_data_buffer.get());
            heap->write_buffer(alloc, 5, vk_buf->device_address(), vk_buf->get_size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        }
    }

    uint32_t VulkanRendererAPI::get_meshlet_frame_slot() {
        auto* ctx = get_vulkan_context();
        HN_CORE_ASSERT(ctx, "get_meshlet_frame_slot: no active VulkanContext");
        return ctx->get_current_frame() % GlobalMeshletBuffers::k_frame_ring_size;
    }

    Ref<StorageBuffer> VulkanRendererAPI::get_mesh_draw_data_buffer(const GlobalMeshletBuffers& bufs) {
        const uint32_t slot = get_meshlet_frame_slot();
        return bufs.draw_data_buffers[slot];
    }

    uint32_t VulkanRendererAPI::get_mesh_block_offset(const GlobalMeshletBuffers& bufs) {
        const uint32_t slot = get_meshlet_frame_slot();
        HN_CORE_ASSERT(bufs.meshlet_blocks[slot].valid,
            "get_mesh_block_offset: meshlet heap block not allocated for this mesh");
        return bufs.meshlet_blocks[slot].offset;
    }

    void VulkanRendererAPI::submit_instanced_draw(
        const Ref<VertexArray>& vertex_array,
        const Ref<VertexBuffer>& instance_vb,
        uint32_t index_count,
        uint32_t instance_count,
        uint32_t instance_byte_offset
    ) {
        require_frame_begun();
        HN_CORE_ASSERT(vertex_array, "submit_instanced_draw: vertex_array is null");
        HN_CORE_ASSERT(instance_vb, "submit_instanced_draw: instance_vb is null");
        HN_CORE_ASSERT(instance_count > 0, "submit_instanced_draw: instance_count must be > 0");
        HN_CORE_ASSERT((instance_byte_offset % 4u) == 0u, "submit_instanced_draw: instance_byte_offset must be 4-byte aligned");

        do_draw_indexed(s_recording_context->get_recording_cmd(),
                        vertex_array, index_count, instance_count,
                        &instance_vb, &instance_byte_offset, 1);
    }

    void VulkanRendererAPI::set_wireframe(bool) {
        if (auto* vk = get_vulkan_context())
            vk->mark_pipeline_dirty();
    }

    void VulkanRendererAPI::set_depth_test(bool) {
        if (auto* vk = get_vulkan_context())
            vk->mark_pipeline_dirty();
    }

    void VulkanRendererAPI::set_depth_write(bool) {
        if (auto* vk = get_vulkan_context())
            vk->mark_pipeline_dirty();
    }

    void VulkanRendererAPI::set_blend(bool) {
        if (auto* vk = get_vulkan_context())
            vk->mark_pipeline_dirty();
    }

    void VulkanRendererAPI::set_blend_for_attachment(uint32_t, bool) {
    }

    void VulkanRendererAPI::set_vsync(bool mode) {
        if (auto* vk = get_vulkan_context())
            vk->request_swapchain_recreation();
    }

    void VulkanRendererAPI::set_cull_mode(CullMode mode) {
        if (auto* vk = get_vulkan_context())
            vk->mark_pipeline_dirty();
    }

    Ref<VertexBuffer> VulkanRendererAPI::create_vertex_buffer(uint32_t size) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanVertexBuffer>(m_device, m_physical_device, size);
    }

    Ref<VertexBuffer> VulkanRendererAPI::create_vertex_buffer(float* vertices, uint32_t size) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanVertexBuffer>(m_device, m_physical_device, vertices, size);
    }

    Ref<IndexBuffer> VulkanRendererAPI::create_index_buffer_u32(uint32_t* indices, uint32_t size) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanIndexBuffer>(m_device, m_physical_device, indices, size);
    }

    Ref<IndexBuffer> VulkanRendererAPI::create_index_buffer_u16(uint16_t* indices, uint32_t size) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanIndexBuffer>(m_device, m_physical_device, indices, size);
    }

    Ref<VertexArray> VulkanRendererAPI::create_vertex_array() {
        return CreateRef<VulkanVertexArray>();
    }

    Ref<UniformBuffer> VulkanRendererAPI::create_uniform_buffer(uint32_t size, uint32_t binding) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanUniformBuffer>(m_device, m_physical_device, size, binding);
    }

    Ref<StorageBuffer> VulkanRendererAPI::create_storage_buffer(uint32_t size, StorageBufferUsage usage_flags) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanStorageBuffer>(m_device, m_physical_device, size, usage_flags);
    }

    Ref<Framebuffer> VulkanRendererAPI::create_framebuffer(const FramebufferSpecification& spec) {
        return CreateRef<VulkanFramebuffer>(spec, &Application::get().get_vulkan_backend());
    }

    void VulkanRendererAPI::submit_camera(const CameraUBO& camera) {
        require_frame_begun();
        auto& p = s_recording_context->pending_globals();
        p.cameraUBO = camera;
        p.hasCamera = true;
    }

    void VulkanRendererAPI::flush_globals() {
        require_frame_begun();
        auto* ctx = s_recording_context;
        auto& p   = ctx->pending_globals();

        ctx->apply_pending_globals(ctx->get_recording_cmd(),
                                   ctx->get_current_pipeline_layout(),
                                   ctx->get_current_frame(), p);

        p.hasCamera    = false;
        p.hasTextures  = false;
        p.textureCount = 0;
        p.textures     = {};
    }

    void VulkanRendererAPI::flush_globals_to_heap() {
        require_frame_begun();
        auto* ctx = s_recording_context;
        HN_CORE_ASSERT(ctx->pending_globals().hasCamera, "flush_globals_heap: no camera submitted");
        ctx->flush_globals_to_heap();   // writes all 5 globals into m_globals_mapped
        ctx->pending_globals().hasCamera = false;
    }

    void VulkanRendererAPI::submit_bound_textures(const std::array<void*, k_max_texture_slots>& textures, uint32_t texture_count) {
        require_frame_begun();
        auto& p = s_recording_context->pending_globals();

        HN_CORE_ASSERT(texture_count > 0, "VulkanRendererAPI::submit_bound_textures called with texture_count == 0");
        HN_CORE_ASSERT(texture_count <= k_max_texture_slots,
                       "VulkanRendererAPI::submit_bound_textures texture_count ({0}) exceeds k_max_texture_slots ({1})",
                       texture_count, k_max_texture_slots);
        HN_CORE_ASSERT(textures[0],
                       "VulkanRendererAPI::submit_bound_textures requires textures[0] to be a valid fallback (e.g. white texture)");

        void* fallback = textures[0];

        p.textures.resize(texture_count);
        for (uint32_t i = 0; i < texture_count; ++i) {
            p.textures[i] = textures[i] ? textures[i] : fallback;
        }

        p.textureCount = texture_count;
        p.hasTextures = true;
    }

    void VulkanRendererAPI::submit_push_constants_mat4(const glm::mat4& value) {
        require_frame_begun();
        VkCommandBuffer cmd = s_recording_context->get_recording_cmd();
        VkPipelineLayout layout = s_recording_context->get_current_pipeline_layout();
        HN_CORE_ASSERT(layout != VK_NULL_HANDLE,
            "submit_push_constants_mat4: no pipeline layout bound yet. Call bind_pipeline() first.");
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &value);
    }

    void VulkanRendererAPI::submit_push_constants(const void* data, uint32_t size, uint32_t offset, VkShaderStageFlags stageFlags) {
        require_frame_begun();
        HN_CORE_ASSERT((size + offset) <= 128, "submit_push_constants: data exceeds 128-byte push constant limit");
        VkCommandBuffer cmd = s_recording_context->get_recording_cmd();
        VkPipelineLayout layout = s_recording_context->get_current_pipeline_layout();
        HN_CORE_ASSERT(layout != VK_NULL_HANDLE,
            "submit_push_constants: no pipeline layout bound yet. Call bind_pipeline() first.");
        vkCmdPushConstants(cmd, layout, stageFlags, offset, size, data);
    }

    bool VulkanRendererAPI::consume_bound_textures(std::array<void*, k_max_texture_slots>& out_textures, uint32_t& out_texture_count) {
        auto& p = s_recording_context->pending_globals();
        if (!p.hasTextures)
            return false;

        out_texture_count = p.textureCount;
        for (uint32_t i = 0; i < p.textureCount; ++i)
            out_textures[i] = p.textures[i];

        p.hasTextures = false;
        p.textures.clear();
        p.textureCount = 0;
        return true;
    }

    void VulkanRendererAPI::submit_lights(const LightsUBO& lights) {
        require_frame_begun();
        s_recording_context->pending_globals().lightUBO = lights;
    }

    void VulkanRendererAPI::submit_tiled_lighting(const TiledLightingData& data) {
        require_frame_begun();
        s_recording_context->pending_globals().tiledLighting = data;
    }

    void VulkanRendererAPI::submit_materials(const std::vector<GPUMaterial>& materials, uint32_t materials_ssbo_offset) {
        require_frame_begun();
        auto& p = s_recording_context->pending_globals();
        p.materials = materials;
        p.materials_ssbo_offset = materials_ssbo_offset;
    }

    VulkanRendererAPI::GlobalsState VulkanRendererAPI::get_globals_state() {
        require_frame_begun();
        const auto& p = s_recording_context->pending_globals();

        GlobalsState s{};
        s.cameraUBO     = p.cameraUBO;
        s.hasCamera     = p.hasCamera;
        s.lightUBO      = p.lightUBO;
        s.tiledLighting = p.tiledLighting;
        s.textures      = p.textures;
        s.textureCount  = p.textureCount;
        s.hasTextures   = p.hasTextures;
        s.source        = static_cast<GlobalsState::Source>(p.source);

        return s;
    }

    void VulkanRendererAPI::set_globals_state(const GlobalsState& state) {
        require_frame_begun();
        auto& p = s_recording_context->pending_globals();

        p.cameraUBO             = state.cameraUBO;
        p.hasCamera             = state.hasCamera;
        p.lightUBO              = state.lightUBO;
        p.tiledLighting         = state.tiledLighting;
        p.textures              = state.textures;
        p.textureCount          = state.textureCount;
        p.hasTextures           = state.hasTextures;
        p.source                = static_cast<VulkanContext::PendingGlobals::Source>(state.source);
    }

    // ---------------------------------------------------------------------------
    // Meshlet heap-block management (set=1, persistent region of the resource heap)
    // ---------------------------------------------------------------------------

    void VulkanRendererAPI::allocate_meshlet_heap_blocks(GlobalMeshletBuffers& bufs) {
        auto* ctx = get_vulkan_context();
        HN_CORE_ASSERT(ctx, "allocate_meshlet_heap_blocks: no active VulkanContext");
        auto* heap = ctx->get_backend()->get_descriptor_heap();

        // Bindings 0-4 are stable for the mesh's lifetime; binding 5 (draw data) is written
        // separately per frame slot by update_mesh_draw_data_binding as it grows.
        VulkanStorageBuffer* stable_bufs[5] = {
            static_cast<VulkanStorageBuffer*>(bufs.vertex_buffer.get()),
            static_cast<VulkanStorageBuffer*>(bufs.meshlets_buffer.get()),
            static_cast<VulkanStorageBuffer*>(bufs.meshlet_vertices_buffer.get()),
            static_cast<VulkanStorageBuffer*>(bufs.meshlet_triangles_buffer.get()),
            static_cast<VulkanStorageBuffer*>(bufs.meshlet_bounds_buffer.get()),
        };

        for (uint32_t slot = 0; slot < GlobalMeshletBuffers::k_frame_ring_size; ++slot) {
            auto alloc = heap->allocate_persistent_block(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 6);
            for (uint32_t i = 0; i < 5; ++i) {
                HN_CORE_ASSERT(stable_bufs[i], "allocate_meshlet_heap_blocks: SSBO buffer {0} is null", i);
                heap->write_buffer(alloc, i, stable_bufs[i]->device_address(), stable_bufs[i]->get_size(), VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
            }
            bufs.meshlet_blocks[slot] = { alloc.offset, alloc.size, alloc.stride, true };
        }
    }

    void VulkanRendererAPI::free_meshlet_heap_blocks(GlobalMeshletBuffers& bufs) {
        auto* ctx = get_vulkan_context();
        if (!ctx) return;
        auto* backend = ctx->get_backend();

        // Frame-fenced like bindless texture indices: a prior frame's command buffer may still be
        // reading this block, so the actual free_persistent_block happens once k_deferred_destroy_frame_lag
        // frames have retired (see VulkanBackend::collect_deferred_destroys_locked).
        for (auto& block : bufs.meshlet_blocks) {
            if (!block.valid) continue;
            VulkanBackend::RetiredTextureResources retired{};
            retired.persistent_block = VulkanDescriptorHeap::Allocation{block.offset, block.size, block.stride};
            backend->defer_destroy_texture_resources(retired);
            block = {};
        }
    }

    void VulkanRendererAPI::push_meshlet_pass_data(uint32_t resource_heap_base, uint32_t draw_data_base) {
        require_frame_begun();
        auto* ctx = get_vulkan_context();
        HN_CORE_ASSERT(ctx, "push_meshlet_pass_data: no active VulkanContext");
        auto* heap = ctx->get_backend()->get_descriptor_heap();

        // Reuses PassPushData's reserved `pad` word to carry draw_data_base — the meshlet shaders'
        // push_constant block reads it under that name (same 16-byte layout, offsetof(resource_heap_base) == 0
        // still matches the set=1 PUSH_INDEX mapping).
        PassPushData pd{};
        pd.resource_heap_base = resource_heap_base;
        pd.sampler_heap_base  = 0;
        pd.flags              = 0;
        pd.pad                = draw_data_base;

        ctx->queue_custom_vulkan_cmd([heap, pd](VkCommandBuffer cmd, uint32_t, uint32_t) {
            heap->push_pass_data(cmd, &pd, sizeof(pd));
        });
    }

    void VulkanRendererAPI::submit_mesh_tasks_indirect_count(
        VkBuffer draw_buffer, VkDeviceSize draw_offset,
        VkBuffer count_buffer, VkDeviceSize count_offset,
        uint32_t max_draws, uint32_t stride)
    {
        require_frame_begun();
        auto* ctx = get_vulkan_context();
        HN_CORE_ASSERT(ctx, "submit_mesh_tasks_indirect_count: no active VulkanContext");

        static PFN_vkCmdDrawMeshTasksIndirectCountEXT fn =
            reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectCountEXT>(
                vkGetDeviceProcAddr(ctx->get_device(), "vkCmdDrawMeshTasksIndirectCountEXT"));
        HN_CORE_ASSERT(fn, "submit_mesh_tasks_indirect_count: vkCmdDrawMeshTasksIndirectCountEXT not available");

        ctx->queue_custom_vulkan_cmd([draw_buffer, draw_offset, count_buffer, count_offset,
                                      max_draws, stride]
            (VkCommandBuffer cmd, uint32_t, uint32_t)
        {
            fn(cmd, draw_buffer, draw_offset, count_buffer, count_offset, max_draws, stride);
        });
    }

} // namespace Honey
