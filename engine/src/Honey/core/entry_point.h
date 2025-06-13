#pragma once
#include "Honey.h"

int main(int argc, char** argv) {
    Honey::Log::init();
    HN_CORE_WARN("Initialized core log!");

#if defined(HN_PLATFORM_MACOS)
    // macOS‑only init (if you need any Obj‑C setup, etc.)
#elif defined(HN_PLATFORM_WINDOWS)
    // Windows‑only init (if anything special)
#endif

    auto app = Honey::create_application();
    app->run();
    delete app;
    return 0;
}