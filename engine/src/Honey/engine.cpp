#include <Honey.h>
#include "engine.h"

#include <glad/glad.h>

namespace Honey {

#define BIND_EVENT_FN(x) std::bind(&x, this, std::placeholders::_1)

    Application* Application::s_instance = nullptr;

    Application::Application() {
        HN_CORE_ASSERT(!s_instance, "Application already exists!");
        s_instance = this;

        m_window = std::unique_ptr<Window>(Window::create());
        m_window->set_event_callback([this](auto && PH1) { on_event(std::forward<decltype(PH1)>(PH1)); });

    }

    Application::~Application() {}

    void Application::push_layer(Layer *layer) {
        m_layer_stack.push_layer(layer);
        layer->on_attach();
    }

    void Application::push_overlay(Layer *layer) {
        m_layer_stack.push_overlay(layer);
        layer->on_attach();
    }



    void Application::on_event(Event& e) {
        EventDispatcher dispatcher(e);

        dispatcher.dispatch<WindowCloseEvent>([this](WindowCloseEvent& e) -> bool {
            return on_window_close(e);
        });

        for (auto it = m_layer_stack.end(); it != m_layer_stack.begin(); ) {
            (*--it)->on_event(e);
            if (e.handled()) {
                break;
            }
        }
    }







    void Application::run() {
        while (m_running)
        {
            glClearColor(1, 0, 1, 1);
            glClear(GL_COLOR_BUFFER_BIT);

            for (Layer* layer : m_layer_stack) {
                layer->on_update();
            }

            m_window->on_update();
        }
    }

    bool Application::on_window_close(WindowCloseEvent &e) {
        m_running = false;
        return true;
    }

}