
#include "core.h"
#include "events/event.h"

namespace Honey {

    class HONEY_API Application {
        public:
        Application();
        virtual ~Application();

        void run();
    };

    //To be defined in client
    HONEY_API Application* create_application();
}
