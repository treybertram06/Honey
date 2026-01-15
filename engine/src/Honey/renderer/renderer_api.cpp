#include "hnpch.h"
#include "renderer_api.h"
#include "Honey/core/settings.h"

namespace Honey {

    auto& renderer_settings = get_settings().renderer;
    RendererAPI::API RendererAPI::s_api = renderer_settings.api;

}