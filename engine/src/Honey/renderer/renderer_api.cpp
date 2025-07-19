#include "hnpch.h"
#include "renderer_api.h"

namespace Honey {

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
    RendererAPI::API RendererAPI::s_api = RendererAPI::API::opengl;
#endif
#if defined(HN_PLATFORM_MACOS)
    RendererAPI::API RendererAPI::s_api = RendererAPI::API::metal;
#endif
}