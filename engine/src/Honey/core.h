#pragma once

// ——————————————————————————————————————————————————————————————————
// 1) Cross‑platform “break into debugger”
// ——————————————————————————————————————————————————————————————————
#if defined(HN_PLATFORM_WINDOWS)
    // MSVC
    #define HN_DEBUGBREAK() __debugbreak()
#elif defined(__clang__) || defined(__GNUC__)
    // Clang/GCC on macOS & Linux
    #include <csignal>
    #if defined(__has_builtin)
        #if __has_builtin(__builtin_debugtrap)
            // best: emits actual trap instruction
            #define HN_DEBUGBREAK() __builtin_debugtrap()
        #else
            #define HN_DEBUGBREAK() raise(SIGTRAP)
        #endif
    #else
        #define HN_DEBUGBREAK() raise(SIGTRAP)
    #endif
#else
    // fallback: no-op
    #define HN_DEBUGBREAK()
#endif

// ——————————————————————————————————————————————————————————————————
// 2) Export / import macros
// ——————————————————————————————————————————————————————————————————
#if defined(HN_PLATFORM_WINDOWS)

    #ifdef HN_BUILD_DLL
        #define HONEY_API __declspec(dllexport)
    #else
        #define HONEY_API __declspec(dllimport)
    #endif

#elif defined(HN_PLATFORM_MACOS) || defined(HN_PLATFORM_LINUX)

    #if defined(HN_BUILD_DYLIB) || defined(HN_BUILD_SHARED)
        // default‐visibility on macOS & Linux
        #define HONEY_API __attribute__((visibility("default")))
    #else
        #define HONEY_API
    #endif

#else
    #define HONEY_API
#endif

// ——————————————————————————————————————————————————————————————————
// 3) Assertion macros (only if enabled)
// ——————————————————————————————————————————————————————————————————
#if defined(HN_ENABLE_ASSERTS)

    #define HN_ASSERT(x, ...)                                                    \
        do {                                                                      \
            if (!(x)) {                                                           \
                HN_ERROR("Assertion Failed: {0}", __VA_ARGS__);                  \
                HN_DEBUGBREAK();                                                  \
            }                                                                     \
        } while (0)

    #define HN_CORE_ASSERT(x, ...)                                               \
        do {                                                                      \
            if (!(x)) {                                                           \
                HN_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__);             \
                HN_DEBUGBREAK();                                                  \
            }                                                                     \
        } while (0)

#else
    #define HN_ASSERT(x, ...)
    #define HN_CORE_ASSERT(x, ...)
#endif

// ——————————————————————————————————————————————————————————————————
// 4) Utility
// ——————————————————————————————————————————————————————————————————
#define BIT(x) (1 << x)

#define HN_BIND_EVENT_FN(fn) std::bind(&fn, this, std::placeholders::_1)
