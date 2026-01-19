#include "hnpch.h"
#include "vk_renderer_api.h"

#include "vk_buffer.h"
#include "vk_vertex_array.h"
#include "Honey/core/engine.h"

namespace Honey {

    struct VulkanDrawRequest {
        Ref<VertexArray> va;
        uint32_t index_count = 0;
        uint32_t instance_count = 0;
        bool valid = false;
    };

    struct VulkanFrameState {
        glm::mat4 view_projection{1.0f};
        bool has_camera = false;

        std::array<void*, VulkanRendererAPI::k_max_texture_slots> textures{};
        uint32_t texture_count = 0;
        bool has_textures = false;
    };

    static VulkanDrawRequest s_draw;
    static VulkanFrameState s_frame;

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
        s_clear_color = color;
    }

    void VulkanRendererAPI::set_viewport(uint32_t, uint32_t, uint32_t, uint32_t) {
        // handled by swapchain extent for now
    }

    void VulkanRendererAPI::clear() {
        s_clear_requested = true;
    }

    void VulkanRendererAPI::draw_indexed(const Ref<VertexArray>& vertex_array, uint32_t index_count) {
        HN_CORE_ASSERT(vertex_array, "Vulkan draw_indexed: vertex_array is null");
        s_draw.va = vertex_array;
        s_draw.index_count = index_count;
        s_draw.instance_count = 1;
        s_draw.valid = true;
    }

    void VulkanRendererAPI::draw_indexed_instanced(const Ref<VertexArray>& vertex_array, uint32_t index_count, uint32_t instance_count) {
        HN_CORE_ASSERT(vertex_array, "Vulkan draw_indexed_instanced: vertex_array is null");
        HN_CORE_ASSERT(instance_count > 0, "Vulkan draw_indexed_instanced: instance_count must be > 0");
        s_draw.va = vertex_array;
        s_draw.index_count = index_count;
        s_draw.instance_count = instance_count;
        s_draw.valid = true;
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

    void VulkanRendererAPI::submit_camera_view_projection(const glm::mat4& view_projection) {
        s_frame.view_projection = view_projection;
        s_frame.has_camera = true;
    }

    bool VulkanRendererAPI::consume_camera_view_projection(glm::mat4& out_view_projection) {
        if (!s_frame.has_camera)
            return false;

        out_view_projection = s_frame.view_projection;
        s_frame.has_camera = false;
        return true;
    }

    bool VulkanRendererAPI::consume_draw_request(Ref<VertexArray>& out_va, uint32_t& out_index_count, uint32_t& out_instance_count) {
        if (!s_draw.valid)
            return false;

        out_va = s_draw.va;
        out_index_count = s_draw.index_count;
        out_instance_count = s_draw.instance_count;

        s_draw = {};
        return true;
    }

    glm::vec4 VulkanRendererAPI::consume_clear_color() {
        return s_clear_color;
    }

    bool VulkanRendererAPI::consume_clear_requested() {
        bool was = s_clear_requested;
        s_clear_requested = false;
        return was;
    }

    void VulkanRendererAPI::submit_bound_textures(const std::array<void*, k_max_texture_slots>& textures, uint32_t texture_count) {
        s_frame.textures = textures;
        s_frame.texture_count = texture_count;
        s_frame.has_textures = true;
    }

    bool VulkanRendererAPI::consume_bound_textures(std::array<void*, k_max_texture_slots>& out_textures, uint32_t& out_texture_count) {
        if (!s_frame.has_textures)
            return false;

        out_textures = s_frame.textures;
        out_texture_count = s_frame.texture_count;

        s_frame.has_textures = false;
        s_frame.textures = {};
        s_frame.texture_count = 0;
        return true;
    }

} // namespace Honey