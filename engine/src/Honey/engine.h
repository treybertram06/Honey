#pragma once

#include "core.h"
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
    private:
        bool on_window_close(WindowCloseEvent& e);

        std::unique_ptr<Window> m_window;
        bool m_running = true;
    };

    // To be defined in CLIENT
    Application* create_application();

}
