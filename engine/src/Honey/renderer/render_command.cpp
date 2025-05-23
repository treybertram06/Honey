#include "hnpch.h"
#include "render_command.h"

#include "platform/opengl/opengl_renderer_api.h"

namespace Honey {
    RendererAPI* RenderCommand::s_renderer_api = new OpenGLRendererAPI;
}