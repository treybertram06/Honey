#engine/CMakeLists.txt

option(ENABLE_ASSERTS "Enable HN_ASSERT / HN_CORE_ASSERT checks" ON)

add_library(engine STATIC
        src/Honey/core/engine.cpp
        src/Honey/core/engine.h
        src/Honey/core/core.h
        src/Honey/core/entry_point.h
        src/Honey/core/log.h
        src/Honey/core/log.cpp
        src/Honey/events/event.h
        src/Honey/events/key_event.h
        src/Honey/events/mouse_event.h
        src/Honey/events/application_event.h
        src/hnpch.cpp
        src/window.h
        src/platform/macos/macos_window.h
        src/platform/macos/macos_window.cpp
        src/Honey/core/layer.cpp
        src/Honey/core/layer.h
        src/Honey/core/layer_stack.cpp
        src/Honey/core/layer_stack.h
        src/platform/windows/windows_window.cpp
        src/Honey/imgui/imgui_layer.cpp
        src/Honey/imgui/imgui_layer.h
        src/Honey/core/input.h
        src/platform/windows/windows_input.h
        src/platform/windows/windows_input.cpp
        src/Honey/core/keycodes.h
        src/Honey/core/mouse_button_codes.h
        src/Honey/imgui/imgui_build.cpp
        src/platform/macos/macos_input.h
        src/platform/macos/macos_input.cpp
        src/Honey/renderer/graphics_context.h
        src/platform/opengl/opengl_context.h
        src/platform/opengl/opengl_context.cpp
        src/Honey/renderer/shader.h
        src/Honey/renderer/shader.cpp
        src/Honey/renderer/buffer.h
        src/Honey/renderer/buffer.cpp
        src/platform/opengl/opengl_buffer.h
        src/platform/opengl/opengl_buffer.cpp
        src/Honey/renderer/renderer.h
        src/Honey/renderer/renderer.cpp
        src/Honey/renderer/vertex_array.h
        src/Honey/renderer/vertex_array.cpp
        src/platform/opengl/opengl_vertex_array.h
        src/platform/opengl/opengl_vertex_array.cpp
        src/Honey/renderer/renderer_api.h
        src/Honey/renderer/renderer_api.cpp
        src/Honey/renderer/render_command.h
        src/Honey/renderer/render_command.cpp
        src/platform/opengl/opengl_renderer_api.h
        src/platform/opengl/opengl_renderer_api.cpp
        src/Honey/renderer/camera.h
        src/Honey/renderer/camera.cpp
        src/Honey/core/timestep.h
        src/Honey/core/statistics.h
        src/platform/opengl/opengl_shader.h
        src/platform/opengl/opengl_shader.cpp
        src/Honey/renderer/texture.h
        src/Honey/renderer/texture.cpp
        src/platform/opengl/opengl_texture.h
        src/platform/opengl/opengl_texture.cpp
        vendor/stb_image/stb_image.h
        vendor/stb_image/stb_image.cpp
        src/Honey/camera_controller.h
        src/Honey/camera_controller.cpp
)

# Make sure we define the Windows platform for Visual Studio
if (MSVC)
 target_compile_definitions(engine PRIVATE
         HN_PLATFORM_WINDOWS
         _CRT_SECURE_NO_WARNINGS
 )
endif()



target_compile_definitions(engine PRIVATE
        $<$<PLATFORM_ID:Windows>:HN_PLATFORM_WINDOWS>
)


target_precompile_headers(engine
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/hnpch.h>
)

 # Make the engine header accessible to other targets
 target_include_directories(engine PUBLIC
         ${CMAKE_CURRENT_SOURCE_DIR}
         ${CMAKE_CURRENT_SOURCE_DIR}/src
         ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb_image
         ${CMAKE_CURRENT_BINARY_DIR}
 )

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
        glm
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
