#include "hnpch.h"
#include "vk_vertex_array.h"

namespace Honey {

    void VulkanVertexArray::add_vertex_buffer(const Ref<VertexBuffer>& vertex_buffer) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(vertex_buffer, "VulkanVertexArray::add_vertex_buffer - vertex_buffer is null");
        HN_CORE_ASSERT(vertex_buffer->get_layout().get_elements().size(), "VertexBuffer has no layout!");
        m_vertex_buffers.push_back(vertex_buffer);
    }

    void VulkanVertexArray::set_index_buffer(const Ref<IndexBuffer>& index_buffer) {
        HN_PROFILE_FUNCTION();
        HN_CORE_ASSERT(index_buffer, "VulkanVertexArray::set_index_buffer - index_buffer is null");
        m_index_buffer = index_buffer;
    }

} // namespace Honey