#include "hnpch.h"
#include "buffer.h"

#include "renderer.h"
#include "platform/opengl/opengl_buffer.h"

namespace Honey {

    VertexBuffer* VertexBuffer::create(float *vertices, uint32_t size) {

        switch (Renderer::get_api()) {
            case RendererAPI::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::opengl:   return new OpenGLVertexBuffer(vertices, size);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

    IndexBuffer* IndexBuffer::create(uint32_t *indices, uint32_t size) {

        switch (Renderer::get_api()) {
            case RendererAPI::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::opengl:   return new OpenGLIndexBuffer(indices, size);
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }


}
