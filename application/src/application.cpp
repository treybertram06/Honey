#include <Honey.h>

#include "Honey/entry_point.h"
#include "Honey/events/application_event.h"

namespace Honey {

        Application::Application() {}

        Application::~Application() {}

        void Application::run() {

                WindowResizeEvent e(1280, 720);
                HN_TRACE(e);

                while (true);
        }

}



