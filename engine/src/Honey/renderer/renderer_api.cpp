#include "hnpch.h"
#include "renderer_api.h"

#include "renderer.h"
#include "Honey/core/settings.h"
#include "platform/opengl/opengl_renderer_api.h"
#include "platform/vulkan/vk_renderer_api.h"

namespace Honey {

    RendererAPI::API RendererAPI::s_api = API::none;

    Scope<RendererAPI> RendererAPI::create() {
        switch (s_api) {
        case API::none:
            HN_CORE_ASSERT(false, "RendererAPI::none is not supported.");
            return nullptr;

        case API::opengl:
            return CreateScope<OpenGLRendererAPI>();

        case API::vulkan:
            return CreateScope<VulkanRendererAPI>();
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI.");
        return nullptr;
    }

}
