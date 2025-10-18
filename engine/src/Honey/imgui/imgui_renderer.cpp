#include "hnpch.h"
#include "imgui_renderer.h"

#include "Honey/renderer/renderer.h"
#include "platform/opengl/opengl_imgui_renderer.h"
#include "platform/vulkan/vk_imgui_renderer.h"

namespace Honey {
    Scope<ImGuiRenderer> ImGuiRenderer::create(void *window) {
        switch (Renderer::get_api()) {
            case RendererAPI::API::none:
                HN_CORE_ASSERT(false, "RendererAPI::none is not supported!");
                return nullptr;
            case RendererAPI::API::opengl: {
                auto renderer = CreateScope<OpenGLImGuiRenderer>();
                renderer->init(window);
                return renderer;
            }
            case RendererAPI::API::vulkan: {
                auto renderer = CreateScope<VulkanImGuiRenderer>();
                renderer->init(window);
                return renderer;
            }
        }

        HN_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

}
