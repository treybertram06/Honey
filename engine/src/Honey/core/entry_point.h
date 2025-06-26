#pragma once
#include "Honey.h"

int main(int argc, char** argv) {
    Honey::Log::init();
    HN_PROFILE_BEGIN_SESSION("Startup", "HoneyProfiler-Startup.json");

#if defined(HN_PLATFORM_MACOS)
    // macOS‑only init (if you need any Obj‑C setup, etc.)
#elif defined(HN_PLATFORM_WINDOWS)
    // Windows‑only init (if anything special)
#endif

    auto app = Honey::create_application();
    HN_PROFILE_END_SESSION();
    HN_PROFILE_BEGIN_SESSION("Runtime", "HoneyProfiler-Runtime.json");
    app->run();
    HN_PROFILE_END_SESSION();
    HN_PROFILE_BEGIN_SESSION("Shutdown", "HoneyProfiler-Shutdown.json");
    delete app;
    HN_PROFILE_END_SESSION();

    return 0;
}