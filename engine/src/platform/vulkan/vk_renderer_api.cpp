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

    static VulkanContext::FramePacket& pkt() {
        if (!s_recording_context) {
            auto* base = Application::get().get_window().get_context();
            auto* vk = dynamic_cast<VulkanContext*>(base);
            HN_CORE_ASSERT(vk, "VulkanRendererAPI: no recording context and main window is not Vulkan");
            s_recording_context = vk;
        }
        return s_recording_context->frame_packet();
    }

    static void require_frame_begun() {
        HN_CORE_ASSERT(pkt().frame_begun,
            "VulkanRendererAPI: frame not begun. Call Renderer::begin_frame() before submitting any Vulkan render commands.");
    }

    static bool pkt_has_pending_globals(const VulkanContext::FramePacket& p) {
        return p.hasCamera || p.hasTextures;
    }

    static VulkanContext::FramePacket::Cmd make_bind_globals_cmd_from_pkt(const VulkanContext::FramePacket& p) {
        VulkanContext::FramePacket::Cmd cmd{};
        cmd.type = VulkanContext::FramePacket::CmdType::BindGlobals;

        cmd.globals.hasCamera = p.hasCamera;
        cmd.globals.cameraUBO = p.cameraUBO;

        cmd.globals.lightUBO = p.lightUBO;

        cmd.globals.materials = p.materials;
        cmd.globals.materials_ssbo_offset = p.materials_ssbo_offset;

        cmd.globals.hasTextures = p.hasTextures;
        cmd.globals.textures = p.textures;
        cmd.globals.textureCount = p.textureCount;

        cmd.globals.source = p.sourceTag;

        return cmd;
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

        auto& p = pkt();

        VulkanContext::FramePacket::Cmd bind{};
        bind.type = VulkanContext::FramePacket::CmdType::BindPipeline;
        bind.bindPipeline.pipeline = reinterpret_cast<VkPipeline>(pipeline->get_native_pipeline());
        bind.bindPipeline.layout   = reinterpret_cast<VkPipelineLayout>(pipeline->get_native_pipeline_layout());

        HN_CORE_ASSERT(bind.bindPipeline.pipeline, "VulkanRendererAPI::bind_pipeline: native VkPipeline is null");
        HN_CORE_ASSERT(bind.bindPipeline.layout, "VulkanRendererAPI::bind_pipeline: native VkPipelineLayout is null");

        p.cmds.push_back(bind);

        // NOTE:
        // Do NOT auto-emit BindGlobals here.
        // Globals ordering must be explicit; otherwise stale packet state can be captured.
    }

    void VulkanRendererAPI::set_clear_color(const glm::vec4& color) {
        require_frame_begun();
        pkt().clearColor = color;
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

    void VulkanRendererAPI::draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count) {
        require_frame_begun();
        HN_CORE_ASSERT(vertex_array, "Vulkan draw_indexed: vertex_array is null");

        VulkanContext::FramePacket::Cmd cmd{};
        cmd.type = VulkanContext::FramePacket::CmdType::DrawIndexed;
        cmd.draw.va = vertex_array;
        cmd.draw.indexCount = index_count;
        cmd.draw.instanceCount = 1;

        // Bind whatever VBs the VA owns (2D and non-instanced 3D both work)
        const auto& vbs = vertex_array->get_vertex_buffers();
        HN_CORE_ASSERT(vbs.size() <= VulkanContext::FramePacket::CmdDrawIndexed::k_max_vertex_buffers,
                       "Vulkan draw_indexed: too many vertex buffers for CmdDrawIndexed");

        cmd.draw.vertexBufferCount = static_cast<uint32_t>(vbs.size());
        for (uint32_t i = 0; i < cmd.draw.vertexBufferCount; ++i) {
            cmd.draw.vertexBuffers[i] = vbs[i];
            cmd.draw.vertexBufferByteOffsets[i] = 0;
        }

        pkt().cmds.push_back(cmd);
    }

    void VulkanRendererAPI::draw_indexed_instanced(const Ref<VertexArray>& vertex_array, uint32_t index_count, uint32_t instance_count) {
        require_frame_begun();
        HN_CORE_ASSERT(vertex_array, "Vulkan draw_indexed_instanced: vertex_array is null");
        HN_CORE_ASSERT(instance_count > 0, "Vulkan draw_indexed_instanced: instance_count must be > 0");

        VulkanContext::FramePacket::Cmd cmd{};
        cmd.type = VulkanContext::FramePacket::CmdType::DrawIndexed;
        cmd.draw.va = vertex_array;
        cmd.draw.indexCount = index_count;
        cmd.draw.instanceCount = instance_count;

        // Non-instanced path doesn't specify extra VBs; caller should use submit_instanced_draw
        const auto& vbs = vertex_array->get_vertex_buffers();
        HN_CORE_ASSERT(vbs.size() <= VulkanContext::FramePacket::CmdDrawIndexed::k_max_vertex_buffers,
                       "Vulkan draw_indexed_instanced: too many vertex buffers for CmdDrawIndexed");

        cmd.draw.vertexBufferCount = static_cast<uint32_t>(vbs.size());
        for (uint32_t i = 0; i < cmd.draw.vertexBufferCount; ++i) {
            cmd.draw.vertexBuffers[i] = vbs[i];
            cmd.draw.vertexBufferByteOffsets[i] = 0;
        }

        pkt().cmds.push_back(cmd);
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
        if (!bufs.draw_data_buffer || bufs.draw_data_buffer->get_size() < needed) {
            bufs.draw_data_buffer = StorageBuffer::create(needed, StorageBufferUsage::Dynamic);
            // Force descriptor recreation so binding 5 points to the new buffer
            bufs.descriptor_set = nullptr;
        }
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

        VulkanContext::FramePacket::Cmd cmd{};
        cmd.type = VulkanContext::FramePacket::CmdType::DrawIndexed;
        cmd.draw.va = vertex_array;
        cmd.draw.indexCount = index_count;
        cmd.draw.instanceCount = instance_count;

        // VA VBs + instance VB appended
        const auto& vbs = vertex_array->get_vertex_buffers();
        const size_t total = vbs.size() + 1;
        HN_CORE_ASSERT(total <= VulkanContext::FramePacket::CmdDrawIndexed::k_max_vertex_buffers,
                       "submit_instanced_draw: too many vertex buffers for CmdDrawIndexed");

        cmd.draw.vertexBufferCount = static_cast<uint32_t>(total);
        for (uint32_t i = 0; i < (uint32_t)vbs.size(); ++i) {
            cmd.draw.vertexBuffers[i] = vbs[i];
            cmd.draw.vertexBufferByteOffsets[i] = 0;
        }
        cmd.draw.vertexBuffers[(uint32_t)vbs.size()] = instance_vb;
        cmd.draw.vertexBufferByteOffsets[(uint32_t)vbs.size()] = instance_byte_offset;

        pkt().cmds.push_back(cmd);
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
        auto& p = pkt();
        p.cameraUBO = camera;
        p.hasCamera = true;
    }

    void VulkanRendererAPI::flush_globals() {
        require_frame_begun();
        auto& p = pkt();

        HN_CORE_ASSERT(pkt_has_pending_globals(p),
                       "VulkanRendererAPI::flush_globals called but no globals are pending");

        VulkanContext::FramePacket::Cmd globals = make_bind_globals_cmd_from_pkt(p);
        p.cmds.push_back(globals);

        // After emitting the command, clear the "pending" flags so we don't accidentally reuse them.
        p.hasCamera = false;
        p.hasTextures = false;
        p.textureCount = 0;
        p.textures = {};
    }

    void VulkanRendererAPI::submit_bound_textures(const std::array<void*, k_max_texture_slots>& textures, uint32_t texture_count) {
        require_frame_begun();
        auto& p = pkt();

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

        VulkanContext::FramePacket::Cmd cmd{};
        cmd.type = VulkanContext::FramePacket::CmdType::PushConstantsMat4;
        cmd.pushMat4.value = value;

        pkt().cmds.push_back(cmd);
    }

    void VulkanRendererAPI::submit_push_constants(const void* data, uint32_t size, uint32_t offset, VkShaderStageFlags stageFlags) {
        require_frame_begun();

        VulkanContext::FramePacket::Cmd cmd{};
        cmd.type = VulkanContext::FramePacket::CmdType::PushConstants;
        std::memcpy(cmd.push.bytes.data(), data, size);
        cmd.push.size = size;
        cmd.push.offset = offset;
        cmd.push.stageFlags = stageFlags;

        pkt().cmds.push_back(cmd);
    }

    bool VulkanRendererAPI::consume_bound_textures(std::array<void*, k_max_texture_slots>& out_textures, uint32_t& out_texture_count) {
        auto& p = pkt();
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
        auto& p = pkt();
        p.lightUBO = lights;
    }

    void VulkanRendererAPI::submit_materials(const std::vector<GPUMaterial>& materials, uint32_t materials_ssbo_offset) {
        require_frame_begun();
        auto& p = pkt();
        p.materials = materials;
        p.materials_ssbo_offset = materials_ssbo_offset;
    }

    VulkanRendererAPI::GlobalsState VulkanRendererAPI::get_globals_state() {
        require_frame_begun();
        const auto& p = pkt();

        GlobalsState s{};
        s.cameraUBO = p.cameraUBO;
        s.hasCamera = p.hasCamera;

        s.lightUBO = p.lightUBO;

        s.textures = p.textures;
        s.textureCount = p.textureCount;
        s.hasTextures = p.hasTextures;

        s.source = static_cast<GlobalsState::Source>(p.sourceTag);

        return s;
    }

    void VulkanRendererAPI::set_globals_state(const GlobalsState& state) {
        require_frame_begun();
        auto& p = pkt();

        p.cameraUBO = state.cameraUBO;
        p.hasCamera = state.hasCamera;

        p.lightUBO = state.lightUBO;

        p.textures = state.textures;
        p.textureCount = state.textureCount;
        p.hasTextures = state.hasTextures;

        p.sourceTag = static_cast<VulkanContext::FramePacket::CmdBindGlobals::Source>(state.source);
    }

    // ---------------------------------------------------------------------------
    // Meshlet descriptor set management
    // ---------------------------------------------------------------------------

    static VkDescriptorSetLayout         s_meshlet_set_layout = VK_NULL_HANDLE;
    static std::vector<VkDescriptorPool> s_meshlet_desc_pools;

    static constexpr uint32_t k_meshlet_pool_chunk = 512; // sets per pool slab

    static VkDescriptorPool create_meshlet_pool(VkDevice device) {
        VkDescriptorPoolSize pool_size{};
        pool_size.type            = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        pool_size.descriptorCount = k_meshlet_pool_chunk * 6; // 6 bindings per set
        VkDescriptorPoolCreateInfo pool_ci{};
        pool_ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_ci.maxSets       = k_meshlet_pool_chunk;
        pool_ci.poolSizeCount = 1;
        pool_ci.pPoolSizes    = &pool_size;
        VkDescriptorPool pool = VK_NULL_HANDLE;
        VkResult r = vkCreateDescriptorPool(device, &pool_ci, nullptr, &pool);
        HN_CORE_ASSERT(r == VK_SUCCESS, "create_meshlet_pool: vkCreateDescriptorPool failed");
        return pool;
    }

    void* VulkanRendererAPI::get_or_create_meshlet_set_layout() {
        if (s_meshlet_set_layout)
            return s_meshlet_set_layout;

        VkDevice device = get_vulkan_context()->get_device();

        // 6 SSBO bindings at set=1:
        //   0 = vertex_buffer            (float[] / VertexPNUV)
        //   1 = meshlets_buffer          (meshopt_Meshlet[])
        //   2 = meshlet_vertices_buffer  (uint32_t[])
        //   3 = meshlet_triangles_buffer (uint8_t[] packed)
        //   4 = meshlet_bounds_buffer    (MeshletBounds[])
        //   5 = draw_data_buffer         (GPUDrawData[], per-mesh, written once on descriptor creation)
        VkDescriptorSetLayoutBinding bindings[6]{};
        for (uint32_t i = 0; i < 6; ++i) {
            bindings[i].binding         = i;
            bindings[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags      = VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;
        }
        VkDescriptorSetLayoutCreateInfo layout_ci{};
        layout_ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout_ci.bindingCount = 6;
        layout_ci.pBindings    = bindings;
        VkResult r = vkCreateDescriptorSetLayout(device, &layout_ci, nullptr, &s_meshlet_set_layout);
        HN_CORE_ASSERT(r == VK_SUCCESS, "get_or_create_meshlet_set_layout: vkCreateDescriptorSetLayout failed");

        // Allocate first pool slab
        s_meshlet_desc_pools.push_back(create_meshlet_pool(device));

        return s_meshlet_set_layout;
    }

    void VulkanRendererAPI::ensure_mesh_descriptor_set(GlobalMeshletBuffers& bufs) {
        if (bufs.descriptor_set) return;

        HN_CORE_ASSERT(s_meshlet_set_layout && !s_meshlet_desc_pools.empty(),
            "ensure_mesh_descriptor_set: call get_or_create_meshlet_set_layout first");

        VkDevice device = get_vulkan_context()->get_device();

        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts        = &s_meshlet_set_layout;

        VkDescriptorSet ds = VK_NULL_HANDLE;
        VkResult r = VK_ERROR_OUT_OF_POOL_MEMORY;

        for (int pi = (int)s_meshlet_desc_pools.size() - 1; pi >= 0 && r != VK_SUCCESS; --pi) {
            alloc.descriptorPool = s_meshlet_desc_pools[pi];
            r = vkAllocateDescriptorSets(device, &alloc, &ds);
        }
        if (r != VK_SUCCESS) {
            s_meshlet_desc_pools.push_back(create_meshlet_pool(device));
            alloc.descriptorPool = s_meshlet_desc_pools.back();
            r = vkAllocateDescriptorSets(device, &alloc, &ds);
            HN_CORE_ASSERT(r == VK_SUCCESS, "ensure_mesh_descriptor_set: vkAllocateDescriptorSets failed on fresh pool");
        }

        HN_CORE_ASSERT(bufs.draw_data_buffer,
            "ensure_mesh_descriptor_set: call update_mesh_draw_data_binding before ensure_mesh_descriptor_set");

        Ref<StorageBuffer> ssbo_bufs[6] = {
            bufs.vertex_buffer,
            bufs.meshlets_buffer,
            bufs.meshlet_vertices_buffer,
            bufs.meshlet_triangles_buffer,
            bufs.meshlet_bounds_buffer,
            bufs.draw_data_buffer
        };
        VkDescriptorBufferInfo buf_infos[6]{};
        VkWriteDescriptorSet   writes[6]{};
        for (uint32_t i = 0; i < 6; ++i) {
            HN_CORE_ASSERT(ssbo_bufs[i], "ensure_mesh_descriptor_set: SSBO buffer {} is null", i);
            buf_infos[i].buffer = reinterpret_cast<VkBuffer>(ssbo_bufs[i]->get_native_buffer());
            buf_infos[i].offset = 0;
            buf_infos[i].range  = VK_WHOLE_SIZE;

            writes[i].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet          = ds;
            writes[i].dstBinding      = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo     = &buf_infos[i];
        }
        vkUpdateDescriptorSets(device, 6, writes, 0, nullptr);

        bufs.descriptor_set = ds;
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

    void VulkanRendererAPI::submit_set1_descriptor_set(void* descriptor_set, void* pipeline_layout) {
        require_frame_begun();
        auto* ctx = get_vulkan_context();
        HN_CORE_ASSERT(ctx, "submit_set1_descriptor_set: no active VulkanContext");

        auto ds  = reinterpret_cast<VkDescriptorSet>(descriptor_set);
        auto lay = reinterpret_cast<VkPipelineLayout>(pipeline_layout);

        ctx->queue_custom_vulkan_cmd([ds, lay](VkCommandBuffer cmd, uint32_t, uint32_t) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    lay, 1, 1, &ds, 0, nullptr);
        });
    }

    void VulkanRendererAPI::destroy_meshlet_resources() {
        auto* ctx = get_vulkan_context();
        if (!ctx) return;
        VkDevice device = ctx->get_device();

        for (VkDescriptorPool pool : s_meshlet_desc_pools)
            vkDestroyDescriptorPool(device, pool, nullptr);
        s_meshlet_desc_pools.clear();

        if (s_meshlet_set_layout) {
            vkDestroyDescriptorSetLayout(device, s_meshlet_set_layout, nullptr);
            s_meshlet_set_layout = VK_NULL_HANDLE;
        }
    }

} // namespace Honey