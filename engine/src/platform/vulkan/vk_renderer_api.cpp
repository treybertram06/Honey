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
        cmd.globals.viewProjection = p.viewProjection;

        cmd.globals.hasTextures = p.hasTextures;
        cmd.globals.textures = p.textures;
        cmd.globals.textureCount = p.textureCount;

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

        // If any old code already pushed BindGlobals earlier in the frame, it will now
        // be incorrectly ordered. To avoid the assert, we conservatively "null out"
        // any previously queued BindGlobals by clearing their payload.
        // (After this migration, you can remove this loop.)
        for (auto& c : p.cmds) {
            if (c.type == VulkanContext::FramePacket::CmdType::BindGlobals) {
                c.globals.hasCamera = false;
                c.globals.hasTextures = false;
                c.globals.textureCount = 0;
                c.globals.textures = {};
            }
        }

        // Defer->flush: emit BindGlobals immediately after binding pipeline (layout is now known)
        if (pkt_has_pending_globals(p)) {
            VulkanContext::FramePacket::Cmd globals = make_bind_globals_cmd_from_pkt(p);
            p.cmds.push_back(globals);

            // Keep the "latest globals" cached too; do NOT clear them here if other passes
            // in the same frame may also need them. If you want "one-shot", clear here.
            // p.hasCamera = false; p.hasTextures = false; ... (not doing that yet)
        }
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

    Ref<VertexBuffer> VulkanRendererAPI::create_vertex_buffer(uint32_t size) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanVertexBuffer>(m_device, m_physical_device, size);
    }

    Ref<VertexBuffer> VulkanRendererAPI::create_vertex_buffer(float* vertices, uint32_t size) {
        HN_CORE_ASSERT(m_device && m_physical_device, "VulkanRendererAPI not initialized (device not available)");
        return CreateRef<VulkanVertexBuffer>(m_device, m_physical_device, vertices, size);
    }

    Ref<IndexBuffer> VulkanRendererAPI::create_index_buffer(uint32_t* indices, uint32_t size) {
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

    Ref<Framebuffer> VulkanRendererAPI::create_framebuffer(const FramebufferSpecification& spec) {
        return CreateRef<VulkanFramebuffer>(spec, &Application::get().get_vulkan_backend());
    }

    void VulkanRendererAPI::submit_camera_view_projection(const glm::mat4& view_projection) {
        require_frame_begun();
        auto& p = pkt();
        p.viewProjection = view_projection;
        p.hasCamera = true;
    }

    void VulkanRendererAPI::submit_bound_textures(const std::array<void*, k_max_texture_slots>& textures, uint32_t texture_count) {
        require_frame_begun();
        auto& p = pkt();
        p.textures = textures;
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

    bool VulkanRendererAPI::consume_bound_textures(std::array<void*, k_max_texture_slots>& out_textures, uint32_t& out_texture_count) {
        auto& p = pkt();
        if (!p.hasTextures)
            return false;

        out_textures = p.textures;
        out_texture_count = p.textureCount;

        p.hasTextures = false;
        p.textures = {};
        p.textureCount = 0;
        return true;
    }

    VulkanRendererAPI::GlobalsState VulkanRendererAPI::get_globals_state() {
        require_frame_begun();
        const auto& p = pkt();

        GlobalsState s{};
        s.viewProjection = p.viewProjection;
        s.hasCamera = p.hasCamera;

        s.textures = p.textures;
        s.textureCount = p.textureCount;
        s.hasTextures = p.hasTextures;

        return s;
    }

    void VulkanRendererAPI::set_globals_state(const GlobalsState& state) {
        require_frame_begun();
        auto& p = pkt();

        p.viewProjection = state.viewProjection;
        p.hasCamera = state.hasCamera;

        p.textures = state.textures;
        p.textureCount = state.textureCount;
        p.hasTextures = state.hasTextures;
    }

} // namespace Honey