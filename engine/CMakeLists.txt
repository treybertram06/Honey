#engine/CMakeLists.txt


option(ENABLE_ASSERTS "Enable HN_ASSERT / HN_CORE_ASSERT checks" ON)

 # Create a static library named "engine"
 add_library(engine STATIC
         src/Honey/engine.cpp
         src/Honey/engine.h
         src/Honey/core.h
         src/Honey/entry_point.h
         src/Honey/log.h
         src/Honey/log.cpp
         src/Honey/events/event.h
         src/Honey/events/key_event.h
         src/Honey/events/mouse_event.h
         src/Honey/events/application_event.h
         src/hnpch.cpp
         src/window.h
         src/platform/macos/macos_window.h
         src/platform/macos/macos_window.cpp
         src/Honey/layer.cpp
         src/Honey/layer.h
         src/Honey/layer_stack.cpp
         src/Honey/layer_stack.h
         src/platform/windows/windows_window.cpp
         src/Honey/imgui/imgui_layer.cpp
         src/Honey/imgui/imgui_layer.h
         src/Honey/input.h
         src/platform/windows/windows_input.h
         src/platform/windows/windows_input.cpp
         src/Honey/keycodes.h
         src/Honey/mouse_button_codes.h
         src/Honey/imgui/imgui_build.cpp
         src/platform/macos/macos_input.h
         src/platform/macos/macos_input.cpp
         src/Honey/renderer/graphics_context.h
         src/platform/opengl/opengl_context.h
         src/platform/opengl/opengl_context.cpp
         #src/platform/opengl/imgui_impl_glfw.cpp
         #src/platform/opengl/imgui_impl_opengl3.cpp
 )

target_precompile_headers(engine
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/hnpch.h>
)

#target_compile_options(engine PRIVATE
#        -Wall -Wextra -Winit-self -Wuninitialized
#)

 # Make the engine header accessible to other targets
 target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
 target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Platform‑specific compile definitions
if (WIN32)
 message(STATUS "Configuring for Windows")
 target_compile_definitions(engine PUBLIC
         HN_PLATFORM_WINDOWS
         GLFW_INCLUDE_NONE
 )
elseif (APPLE)
 message(STATUS "Configuring for macOS")
 target_compile_definitions(engine PUBLIC
         GL_SILENCE_DEPRECATION
         HN_PLATFORM_MACOS
         GLFW_INCLUDE_NONE

 )
elseif (UNIX)
 message(STATUS "Configuring for Linux/Unix")
 target_compile_definitions(engine PUBLIC
         HN_PLATFORM_LINUX
         GLFW_INCLUDE_NONE

 )
else()
 message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

if(ENABLE_ASSERTS)
 target_compile_definitions(engine PUBLIC HN_ENABLE_ASSERTS)
endif()

add_subdirectory(vendor/glad)
add_subdirectory(vendor/imgui)
#add_subdirectory(vendor/glm)

target_link_libraries(engine PRIVATE
        spdlog_header_only
        glfw
        glad
        #glm
        PUBLIC
        imgui
)

if (APPLE)
 #   find OpenGL.framework and link it
 find_library(OPENGL_FRAMEWORK OpenGL)
 if(NOT OPENGL_FRAMEWORK)
  message(FATAL_ERROR "Could not find OpenGL.framework")
 endif()
 target_link_libraries(engine PRIVATE ${OPENGL_FRAMEWORK})
elseif (WIN32)
 target_link_libraries(engine PRIVATE opengl32)     # native .lib
elseif (UNIX)        # Linux / BSD
 find_package(OpenGL REQUIRED)
 target_link_libraries(engine PRIVATE OpenGL::GL)
endif()
