#pragma once

#include "core.h"
#include "Honey/core/timestep.h"

#include "window.h"
#include "layer.h"
#include "layer_stack.h"
#include "../events/event.h"
#include "../events/application_event.h"

#include "Honey/imgui/imgui_layer.h"

#include "Honey/renderer/shader.h"
#include "Honey/renderer/buffer.h"
#include "Honey/renderer/vertex_array.h"
#include "../renderer/camera.h"

namespace Honey {

    class HONEY_API Application
    {
    public:
        Application(const std::string& name = "Honey Application");
        virtual ~Application();

        void run();

        void on_event(Event& e);

        void push_layer(Layer* layer);
        void push_overlay(Layer* layer);

        inline static Application& get() { return *s_instance; }
        inline Window& get_window() { return *m_window; }

        inline static void quit() { m_running = false; }

        ImGuiLayer* get_imgui_layer() { return m_imgui_layer; }
    private:
        bool on_window_close(WindowCloseEvent& e);
        bool on_window_resize(WindowResizeEvent& e);

        Scope<Window> m_window;
        ImGuiLayer* m_imgui_layer;
        static bool m_running;
        bool m_minimized = false;
        LayerStack m_layer_stack;
        float m_last_frame_time = 0.0f;


        static Application* s_instance;


    };

    // To be defined in CLIENT
    Application* create_application();

}
