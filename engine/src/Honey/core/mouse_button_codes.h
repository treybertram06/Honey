#pragma once

#if defined(HN_PLATFORM_WINDOWS) || defined(HN_PLATFORM_LINUX)
    //From glfw3.h
    #define HN_MOUSE_BUTTON_1         0
    #define HN_MOUSE_BUTTON_2         1
    #define HN_MOUSE_BUTTON_3         2
    #define HN_MOUSE_BUTTON_4         3
    #define HN_MOUSE_BUTTON_5         4
    #define HN_MOUSE_BUTTON_6         5
    #define HN_MOUSE_BUTTON_7         6
    #define HN_MOUSE_BUTTON_8         7
    #define HN_MOUSE_BUTTON_LAST      HN_MOUSE_BUTTON_8
    #define HN_MOUSE_BUTTON_LEFT      HN_MOUSE_BUTTON_1
    #define HN_MOUSE_BUTTON_RIGHT     HN_MOUSE_BUTTON_2
    #define HN_MOUSE_BUTTON_MIDDLE    HN_MOUSE_BUTTON_3
#endif

#ifdef HN_PLATFORM_MACOS
//From glfw3.h
  #define HN_MOUSE_BUTTON_1         0
  #define HN_MOUSE_BUTTON_2         1
  #define HN_MOUSE_BUTTON_3         2
  #define HN_MOUSE_BUTTON_4         3
  #define HN_MOUSE_BUTTON_5         4
  #define HN_MOUSE_BUTTON_6         5
  #define HN_MOUSE_BUTTON_7         6
  #define HN_MOUSE_BUTTON_8         7
  #define HN_MOUSE_BUTTON_LAST      HN_MOUSE_BUTTON_8
  #define HN_MOUSE_BUTTON_LEFT      HN_MOUSE_BUTTON_1
  #define HN_MOUSE_BUTTON_RIGHT     HN_MOUSE_BUTTON_2
  #define HN_MOUSE_BUTTON_MIDDLE    HN_MOUSE_BUTTON_3
#endif
