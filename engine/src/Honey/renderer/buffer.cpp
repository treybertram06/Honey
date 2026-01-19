#include "hnpch.h"
#include "buffer.h"

#include "renderer.h"
#include "platform/opengl/opengl_buffer.h"
#include "platform/vulkan/vk_buffer.h"

namespace Honey {

    Ref<VertexBuffer> VertexBuffer::create(uint32_t size) {
        return RenderCommand::get_renderer_api()->create_vertex_buffer(size);
    }

    Ref<VertexBuffer> VertexBuffer::create(float *vertices, uint32_t size) {
        return RenderCommand::get_renderer_api()->create_vertex_buffer(vertices, size);
    }

    Ref<IndexBuffer> IndexBuffer::create(uint32_t *indices, uint32_t count) {
        return RenderCommand::get_renderer_api()->create_index_buffer(indices, count);
    }

    Ref<UniformBuffer> UniformBuffer::create(uint32_t size, uint32_t binding) {
        return RenderCommand::get_renderer_api()->create_uniform_buffer(size, binding);
    }


}
