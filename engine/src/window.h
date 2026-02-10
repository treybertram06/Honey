#pragma once

#include "hnpch.h"

#include "Honey/core/base.h"
#include "Honey/events/event.h"
#include "Honey/renderer/graphics_context.h"

namespace Honey {
    struct WindowProps {
        std::string title;
        uint32_t width;
        uint32_t height;

        WindowProps(const std::string& title = "Honey Engine",
                    uint32_t width = 1280,
                    uint32_t height = 720)
                        : title(title), width(width), height(height) {}
    };

    class HONEY_API Window {
    public:
        using event_callback_fn = std::function<void(Event&)>;

        virtual ~Window() {}

        virtual void on_update() = 0;

        virtual uint32_t get_width() const = 0;
        virtual uint32_t get_height() const = 0;

        //window attributes
        virtual void set_event_callback(const event_callback_fn& callback) = 0;
        virtual void set_vsync(bool enabled) = 0;
        virtual bool is_vsync() const = 0;

        virtual void* get_native_window() const = 0;
        virtual GraphicsContext* get_context() const = 0;

        virtual void request_close() = 0;

        static Scope<Window> create(const WindowProps& props = WindowProps());

    };
}
