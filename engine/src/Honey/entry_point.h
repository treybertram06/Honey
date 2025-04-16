#pragma once

#ifdef HN_PLATFORM_WINDOWS

extern Honey::Engine* Honey::create_application();

int main(int argc, char** argv) {

    //std::cout << "Honey Engine started\n";

    Honey::Log::init();
    HN_CORE_WARN("Initialized core log!");
    int a = 69;
    HN_INFO("Helo: var= {0}", a);

    auto app = Honey::create_application();
    app->Run();
    delete app;
}

#endif
