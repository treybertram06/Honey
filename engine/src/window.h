#pragma once

#include "hnpch.h"

#include "Honey/core/core.h"
#include "Honey/events/event.h"

namespace Honey {
    struct WindowProps {
        std::string title;
        unsigned int width;
        unsigned int height;

        WindowProps(const std::string& title = "Honey Engine",
                    unsigned int width = 1280,
                    unsigned int height = 720)
                        : title(title), width(width), height(height) {}
    };

    class HONEY_API Window {
    public:
        using event_callback_fn = std::function<void(Event&)>;

        virtual ~Window() {}

        virtual void on_update() = 0;

        virtual unsigned int get_width() const = 0;
        virtual unsigned int get_height() const = 0;

        //window attributes
        virtual void set_event_callback(const event_callback_fn& callback) = 0;
        virtual void set_vsync(bool enabled) = 0;
        virtual bool is_vsync() const = 0;

        virtual void* get_native_window() const = 0;

        static Scope<Window> create(const WindowProps& props = WindowProps());

    };
}