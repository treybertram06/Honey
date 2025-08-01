#pragma once

#include <iostream>
#include <memory>
#include <utility>
#include <algorithm>
#include <functional>
#include <numeric>
#include <filesystem>

#include <string>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <map>

#include "Honey/core/log.h"
#include "Honey/core/base.h"
#include "Honey/debug/instrumentor.h"


#if defined(HN_PLATFORM_WINDOWS)
  // slim down Windows.h
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #include <Windows.h>

#elif defined(HN_PLATFORM_MACOS)
  #ifdef __OBJC__
    #import <AppKit/AppKit.h>
    // or #import <Cocoa/Cocoa.h> if you need Foundation+AppKit all together
  #endif

#elif defined(HN_PLATFORM_LINUX)
  #include <unistd.h>
  #include <dlfcn.h>
#endif
