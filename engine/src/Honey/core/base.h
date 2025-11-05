#pragma once
#include <memory>

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
// 2) Static/Dynamic building
// ——————————————————————————————————————————————————————————————————
/*
#if HN_DYNAMIC_LINK
    #ifdef HN_BUILD_DLL
        #define HONEY_API __declspec(dllexport)
    #else
        #define HONEY_API __declspec(dllimport)
    #endif
#else
    #define HONEY_API
#endif
*/
#define HONEY_API
// ——————————————————————————————————————————————————————————————————
// 3) Assertion macros (only if enabled)
// ——————————————————————————————————————————————————————————————————
#if defined(HN_ENABLE_ASSERTS)

#define HN_ASSERT(x, ...)                                                    \
    do {                                                                      \
        if (!(x)) {                                                           \
            if constexpr (sizeof(#__VA_ARGS__) > 1) {                         \
                HN_ERROR("Assertion Failed: {0}", __VA_ARGS__);               \
            } else {                                                          \
                HN_ERROR("Assertion Failed: {0}", #x);                        \
            }                                                                 \
            HN_DEBUGBREAK();                                                  \
        }                                                                     \
    } while (0)

#define HN_CORE_ASSERT(x, ...)                                               \
    do {                                                                      \
        if (!(x)) {                                                           \
            if constexpr (sizeof(#__VA_ARGS__) > 1) {                         \
                HN_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__);          \
            } else {                                                          \
                HN_CORE_ERROR("Assertion Failed: {0}", #x);                   \
            }                                                                 \
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

namespace Honey {

    template<typename T>
    using Scope = std::unique_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Scope<T> CreateScope(Args&& ... args) {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    using Ref = std::shared_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Ref<T> CreateRef(Args&& ... args) {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
}

#if defined(_MSC_VER)
    #define CURRENT_FUNCTION __FUNCSIG__
#elif defined(__GNUC__) || defined(__clang__)
    #define CURRENT_FUNCTION __PRETTY_FUNCTION__
#else
    #define CURRENT_FUNCTION __func__
#endif