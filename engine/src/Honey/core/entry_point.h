#pragma once
#include "Honey.h"
#include "Honey/core/asset_paths.h"

namespace Honey {
    // Defined by each application next to create_application(): whether it may start with no project.
    AssetPaths::Mode asset_paths_mode();
}

int main(int argc, char** argv) {
    Honey::Log::init();

    // Must run before anything touches assets/config, including Application's constructor.
    if (!Honey::AssetPaths::init(argc, argv, Honey::asset_paths_mode()))
        return 1;

    HN_PROFILE_BEGIN_SESSION("Startup", "HoneyProfiler-Startup.json");

#if defined(HN_PLATFORM_MACOS)
    // macOS‑only init (if you need any Obj‑C setup, etc.)
#elif defined(HN_PLATFORM_WINDOWS)
    // Windows‑only init (if anything special)
#endif
    int exit_code = 0;
    auto app = Honey::create_application();
    if (!app) { // create_application already logged why
        HN_PROFILE_END_SESSION();
        return 1;
    }
    HN_PROFILE_END_SESSION();
    HN_PROFILE_BEGIN_SESSION("Runtime", "HoneyProfiler-Runtime.json");
    exit_code = app->run();
    HN_PROFILE_END_SESSION();
    HN_PROFILE_BEGIN_SESSION("Shutdown", "HoneyProfiler-Shutdown.json");
    delete app;
    HN_PROFILE_END_SESSION();

    return exit_code;
}