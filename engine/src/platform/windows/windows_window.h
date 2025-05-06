#pragma once

#include "window.h"
#include <glfw/glfw3.h>

#include "Honey/renderer/graphics_context.h"

namespace Honey {

    class WindowsWindow : public Window {
    public:
        WindowsWindow(const WindowProps& props);
        virtual ~WindowsWindow();

        void on_update() override;

        inline unsigned int get_width() const override { return m_data.width; }
        inline unsigned int get_height() const override { return m_data.height; }

        //window attributes
        inline void set_event_callback(const event_callback_fn &callback) override { m_data.event_callback = callback; }
        void set_vsync(bool enabled) override;
        bool is_vsync() const override;

        inline virtual void* get_native_window() const { return m_window; }

    private:
        virtual void init(const WindowProps& props);
        virtual void shutdown();

        GLFWwindow* m_window;
        GraphicsContext* m_context;

        struct WindowData {
            std::string title;
            unsigned int width, height;
            bool vsync;

            event_callback_fn event_callback;
        };

        WindowData m_data;


    };
}