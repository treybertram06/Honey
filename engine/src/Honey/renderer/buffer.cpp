#include "hnpch.h"
#include "buffer.h"

#include "renderer.h"
#include "platform/metal/metal_buffer.h"
#include "platform/opengl/opengl_buffer.h"

namespace Honey {

    Ref<VertexBuffer> VertexBuffer::create(std::uint32_t size) {

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLVertexBuffer>(size);
            case RendererAPI::API::metal:   return CreateRef<MetalVertexBuffer>(size);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<VertexBuffer> VertexBuffer::create(float *vertices, std::uint32_t size) {

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLVertexBuffer>(vertices, size);
            case RendererAPI::API::metal:   return CreateRef<MetalVertexBuffer>(vertices, size);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    Ref<IndexBuffer> IndexBuffer::create(std::uint32_t *indices, std::uint32_t count) {

        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLIndexBuffer>(indices, count);
            case RendererAPI::API::metal:   return CreateRef<MetalIndexBuffer>(indices, count);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }


}
