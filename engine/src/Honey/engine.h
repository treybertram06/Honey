
#include "core.h"
#include "events/event.h"
#include "window.h"

namespace Honey {

    class HONEY_API Application {
    public:
        Application();
        virtual ~Application();

        void run();

    private:
        std::unique_ptr<Window> m_window;
        bool m_running = true;
    };

    //To be defined in client
    HONEY_API Application* create_application();
}
