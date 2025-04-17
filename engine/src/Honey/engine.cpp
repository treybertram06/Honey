

#include "engine.h"
namespace Honey {
    Application::Application() {

    }
    Application::~Application() {

    }

    void Application::run() {
        while (true);
    }

    HONEY_API Application* create_application() {
        return new Application();
    }

}