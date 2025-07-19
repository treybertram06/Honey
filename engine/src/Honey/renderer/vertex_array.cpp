#include "hnpch.h"
#include "vertex_array.h"

#include "renderer.h"
#include "platform/metal/metal_vertex_array.h"
#include "platform/opengl/opengl_vertex_array.h"

namespace Honey {

    Ref<VertexArray> VertexArray::create() {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:     HN_CORE_ASSERT(false, "RendererAPI::none is not supported."); return nullptr;
            case RendererAPI::API::opengl:   return CreateRef<OpenGLVertexArray>();
            case RendererAPI::API::metal:   return CreateRef<MetalVertexArray>();
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }


}
