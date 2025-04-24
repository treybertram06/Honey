#pragma once

#include "core.h"
#include "layer.h"
#include "layer_stack.h"
#include "events/event.h"
#include "window.h"
#include "events/application_event.h"

namespace Honey {

    class HONEY_API Application
    {
    public:
        Application();
        virtual ~Application();

        void run();

        void on_event(Event& e);

        void push_layer(Layer* layer);
        void push_overlay(Layer* layer);
    private:
        bool on_window_close(WindowCloseEvent& e);

        std::unique_ptr<Window> m_window;
        bool m_running = true;
        LayerStack m_layer_stack;


    };

    // To be defined in CLIENT
    Application* create_application();

}
