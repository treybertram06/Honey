#include "hnpch.h"
#include "buffer.h"

#include "renderer.h"
#include "platform/opengl/opengl_buffer.h"
#include "platform/vulkan/vk_buffer.h"

namespace Honey {

    Ref<VertexBuffer> VertexBuffer::create(uint32_t size) {

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLVertexBuffer>(size);
            case RendererAPI::API::vulkan:   return CreateRef<VulkanVertexBuffer>(size);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<VertexBuffer> VertexBuffer::create(float *vertices, uint32_t size) {

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLVertexBuffer>(vertices, size);
            case RendererAPI::API::vulkan:   return CreateRef<VulkanVertexBuffer>(vertices, size);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<IndexBuffer> IndexBuffer::create(uint32_t *indices, uint32_t count) {

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLIndexBuffer>(indices, count);
            case RendererAPI::API::vulkan:   return CreateRef<VulkanIndexBuffer>(indices, count);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<UniformBuffer> UniformBuffer::create(uint32_t size, uint32_t binding) {

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLUniformBuffer>(size, binding);
            case RendererAPI::API::vulkan:   return CreateRef<VulkanUniformBuffer>(size, binding);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }


}
