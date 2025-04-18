#pragma once

#if defined(HN_PLATFORM_WINDOWS)
  #ifdef HN_BUILD_DLL
    #define HONEY_API __declspec(dllexport)
  #else
    #define HONEY_API __declspec(dllimport)
  #endif

#elif defined(HN_PLATFORM_MACOS)
  #ifdef HN_BUILD_DYLIB
    // On macOS, default visibility exports symbols from a .dylib
    #define HONEY_API __attribute__((visibility("default")))
  #else
    #define HONEY_API
  #endif

#elif defined(HN_PLATFORM_LINUX)
  #ifdef HN_BUILD_SHARED
    #define HONEY_API __attribute__((visibility("default")))
  #else
    #define HONEY_API
  #endif

#else
  #define HONEY_API
#endif

#define BIT(x) (1 << x)