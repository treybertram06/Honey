#include <Honey.h>

#include "Honey/entry_point.h"
#include "Honey/events/application_event.h"

namespace Honey {
/*
        Application::Application() {
                m_window = std::unique_ptr<Window>(Window::create());
        }*/

        Application::Application() {
                HN_CORE_INFO("About to call Window::create");
                Window* raw = Window::create();                 // uses the default WindowProps
                HN_CORE_INFO("Window::create returned {0}", (void*)raw);
                HN_CORE_ASSERT(raw, "Window::create() returned nullptr!");
                m_window.reset(raw);
        }

        Application::~Application() {}

        void Application::run() {

                while (m_running) {
                        m_window->on_update();

                }

        }

}



