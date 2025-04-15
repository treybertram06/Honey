
#include "core.h"

namespace Honey {

    class HONEY_API Engine {
        public:
        Engine();
        virtual ~Engine();

        void Run();
    };

    //To be defined in client
    Engine* create_application();
}
