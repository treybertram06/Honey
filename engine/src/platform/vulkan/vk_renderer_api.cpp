#include "hnpch.h"
#include "vk_renderer_api.h"

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

    static VulkanContext::FramePacket::Cmd& get_or_push_globals_cmd() {
        require_frame_begun();

        auto& p = pkt();
        if (!p.cmds.empty() && p.cmds.back().type == VulkanContext::FramePacket::CmdType::BindGlobals)
            return p.cmds.back();

        VulkanContext::FramePacket::Cmd cmd{};
        cmd.type = VulkanContext::FramePacket::CmdType::BindGlobals;
        p.cmds.push_back(cmd);
        return p.cmds.back();
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
        auto& cmd = get_or_push_globals_cmd();
        cmd.globals.viewProjection = view_projection;
        cmd.globals.hasCamera = true;
    }

    bool VulkanRendererAPI::consume_camera_view_projection(glm::mat4& out_view_projection) {
        auto& p = pkt();
        if (!p.hasCamera)
            return false;

        out_view_projection = p.viewProjection;
        p.hasCamera = false;
        return true;
    }

    bool VulkanRendererAPI::consume_draw_request(Ref<VertexArray>& out_va, uint32_t& out_index_count, uint32_t& out_instance_count) {
        auto& p = pkt();
        if (p.drawCursor >= p.draws.size())
            return false;

        const auto& cmd = p.draws[p.drawCursor++];

        out_va = cmd.va;
        out_index_count = cmd.indexCount;
        out_instance_count = cmd.instanceCount;
        return true;
    }

    glm::vec4 VulkanRendererAPI::consume_clear_color() {
        return pkt().clearColor;
    }

    bool VulkanRendererAPI::consume_clear_requested() {
        auto& p = pkt();
        bool was = p.clearRequested;
        p.clearRequested = false;
        return was;
    }

    void VulkanRendererAPI::submit_bound_textures(const std::array<void*, k_max_texture_slots>& textures, uint32_t texture_count) {
        auto& cmd = get_or_push_globals_cmd();
        cmd.globals.textures = textures;
        cmd.globals.textureCount = texture_count;
        cmd.globals.hasTextures = true;
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

} // namespace Honey