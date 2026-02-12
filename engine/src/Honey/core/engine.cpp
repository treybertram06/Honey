#include "hnpch.h"
#include <Honey.h>
#include "engine.h"
#include "input.h"
#include "Honey/renderer/renderer.h"
#include "Honey/scripting/script_engine.h"

#include "Honey/renderer/camera.h"
#include <GLFW/glfw3.h>

#include "settings.h"

namespace Honey {

#define BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)

    Application* Application::s_instance = nullptr;
    bool Application::m_running = true;



    Application::Application(const std::string& name, int width, int height) {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(!s_instance, "Application already exists!");
        s_instance = this;

        const std::filesystem::path asset_root = ASSET_ROOT;
        Settings::load_from_file( asset_root / ".." / "config" / "settings.yaml" );

        auto& renderer_settings = Settings::get().renderer; // I'm not sure if this is the best place to do this, but I'm also not sure where else I could...
        RendererAPI::set_api(renderer_settings.api);
        RenderCommand::set_renderer_api(RendererAPI::create());

        if (renderer_settings.api == RendererAPI::API::vulkan) {
            // VulkanBackend needs GLFW initialized for glfwVulkanSupported() and instance extensions.
            int glfw_ok = glfwInit();
            HN_CORE_ASSERT(glfw_ok, "Could not initialize GLFW!");

            m_vulkan_backend = std::make_unique<VulkanBackend>();
            m_vulkan_backend->init();
        }

        m_window = Window::create(WindowProps(name, width, height));

        m_window->set_event_callback([this](auto && PH1) { on_event(std::forward<decltype(PH1)>(PH1)); });
        m_window->set_vsync(renderer_settings.vsync);

        Renderer::init();

        m_imgui_layer = new ImGuiLayer();
        push_overlay(m_imgui_layer);

        ScriptEngine::init();


    }

    Application::~Application() {
        HN_PROFILE_FUNCTION();

        HN_CORE_INFO("Application::~Application");


        if (RendererAPI::get_api() == RendererAPI::API::vulkan) {
            auto* ctx = m_window ? m_window->get_context() : nullptr;
            if (ctx) ctx->wait_idle();
        }

        Renderer::shutdown();
        ScriptEngine::shutdown();
        Texture2D::shutdown_cache();

        for (Layer* layer : m_layer_stack) {
            layer->on_detach();
        }
        m_layer_stack.clear();

        m_window.reset();

        if (m_vulkan_backend) {
            m_vulkan_backend->shutdown();
            m_vulkan_backend.reset();
        }
    }

    VulkanBackend& Application::get_vulkan_backend() {
        HN_CORE_ASSERT(m_vulkan_backend, "get_vulkan_backend() called but Vulkan backend is not initialized");
        return *m_vulkan_backend;
    }

    const VulkanBackend& Application::get_vulkan_backend() const {
        HN_CORE_ASSERT(m_vulkan_backend, "get_vulkan_backend() const called but Vulkan backend is not initialized");
        return *m_vulkan_backend;
    }

    void Application::push_layer(Layer *layer) {
        HN_PROFILE_FUNCTION();

        m_layer_stack.push_layer(layer);
        layer->on_attach();
    }

    void Application::push_overlay(Layer *layer) {
        HN_PROFILE_FUNCTION();

        m_layer_stack.push_overlay(layer);
        layer->on_attach();
    }



    void Application::on_event(Event& e) {
        HN_PROFILE_FUNCTION();

        for (auto it = m_layer_stack.rbegin(); it != m_layer_stack.rend(); ++it) {
            if (e.handled())
                break;
            (*it)->on_event(e);
        }

        if (e.handled())
            return;

        EventDispatcher dispatcher(e);

        dispatcher.dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::on_window_close));
        dispatcher.dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::on_window_resize));

    }

    void Application::run() {
        HN_PROFILE_FUNCTION();

        while (m_running)
        {
            HN_PROFILE_SCOPE("Application run loop");

            float time = (float)glfwGetTime();
            Timestep timestep = time - m_last_frame_time;
            m_last_frame_time = time;

            if (!m_minimized) {
                Renderer::begin_frame();

                {
                    HN_PROFILE_SCOPE("LayerStack on_update_runtime");

                    for (Layer* layer : m_layer_stack) {
                        layer->on_update(timestep);
                    }
                }

                auto& renderer_settings = Settings::get().renderer;
                m_imgui_layer->begin();
                {
                    HN_PROFILE_SCOPE("LayerStack on_imgui_render");
                    for (Layer* layer : m_layer_stack) {
                        layer->on_imgui_render();
                    }
                }
                m_imgui_layer->end();

                //if (RendererAPI::get_api() == RendererAPI::API::vulkan) {
                //    // nullptr => main window / swapchain
                //    Renderer::set_render_target(nullptr);
                //    Renderer::begin_pass();
                //    // Any future direct window-space rendering would go here
                //    Renderer::end_pass();
                //}
            }

            m_window->on_update();
        }
    }

    bool Application::on_window_close(WindowCloseEvent &e) {
        if (e.handled())
            return true;

        m_running = false;
        return true;
    }

    bool Application::on_window_resize(WindowResizeEvent &e) {
        HN_PROFILE_FUNCTION();

        if (e.get_width() == 0 || e.get_height() == 0) {
            m_minimized = true;
            return false;
        }

        m_minimized = false;
        Renderer::on_window_resize(e.get_width(), e.get_height());
        return false;
    }


}
