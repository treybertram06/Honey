#include "hnpch.h"
#include <Honey.h>
#include "engine.h"
#include "input.h"
#include "Honey/renderer/renderer.h"

#include "Honey/renderer/camera.h"
#include <GLFW/glfw3.h>

namespace Honey {

#define BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)

    Application* Application::s_instance = nullptr;
    bool Application::m_running = true;



    Application::Application() {
        HN_PROFILE_FUNCTION();

        HN_CORE_ASSERT(!s_instance, "Application already exists!");
        s_instance = this;

        m_window = std::unique_ptr<Window>(Window::create());

        m_window->set_event_callback([this](auto && PH1) { on_event(std::forward<decltype(PH1)>(PH1)); });
        m_window->set_vsync(false);

        Renderer::init();

        m_imgui_layer = new ImGuiLayer();
        push_overlay(m_imgui_layer);


    }

    Application::~Application() {
        HN_PROFILE_FUNCTION();

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

        EventDispatcher dispatcher(e);

        dispatcher.dispatch<WindowCloseEvent>(BIND_EVENT_FN(Application::on_window_close));
        dispatcher.dispatch<WindowResizeEvent>(BIND_EVENT_FN(Application::on_window_resize));

        for (auto it = m_layer_stack.end(); it != m_layer_stack.begin(); ) {
            (*--it)->on_event(e);
            if (e.handled()) {
                break;
            }
        }
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
                {
                    HN_PROFILE_SCOPE("LayerStack on_update");

                    for (Layer* layer : m_layer_stack) {
                        layer->on_update(timestep);
                    }
                }

                m_imgui_layer->begin();
                {
                    HN_PROFILE_SCOPE("LayerStack on_imgui_render");
                    for (Layer* layer : m_layer_stack)
                        layer->on_imgui_render();
                }
                m_imgui_layer->end();
            }

            m_window->on_update();
        }
    }

    bool Application::on_window_close(WindowCloseEvent &e) {
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
