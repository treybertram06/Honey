#pragma once

#include "core.h"

#include "window.h"
#include "layer.h"
#include "layer_stack.h"
#include "events/event.h"
#include "events/application_event.h"

#include "Honey/imgui/imgui_layer.h"

#include "Honey/renderer/shader.h"
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/vertex_array.h"
#include "renderer/camera.h"

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

        inline static Application& get() { return *s_instance; }
        inline Window& get_window() { return *m_window; }
    private:
        bool on_window_close(WindowCloseEvent& e);

        std::unique_ptr<Window> m_window;
        ImGuiLayer* m_imgui_layer;
        bool m_running = true;
        LayerStack m_layer_stack;



        static Application* s_instance;


    };

    // To be defined in CLIENT
    Application* create_application();

}
