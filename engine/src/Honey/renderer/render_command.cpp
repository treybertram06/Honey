#include "hnpch.h"
#include "render_command.h"

#include "platform/opengl/opengl_renderer_api.h"
#if defined(HN_PLATFORM_MACOS)
#include "platform/metal/metal_renderer_api.h"
#endif

namespace Honey {
    #if defined(HN_PLATFORM_MACOS)
    RendererAPI* RenderCommand::s_renderer_api = create_metal_renderer_api();
    #else
    RendererAPI* RenderCommand::s_renderer_api = new OpenGLRendererAPI;
    #endif
}