#engine/CMakeLists.txt

enable_language(OBJCXX)

option(ENABLE_ASSERTS "Enable HN_ASSERT / HN_CORE_ASSERT checks" ON)

add_library(engine STATIC
        src/Honey/core/engine.cpp
        src/Honey/core/engine.h
        src/Honey/core/base.h
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
        src/platform/windows/windows_input.cpp
        src/Honey/core/keycodes.h
        src/Honey/core/mouse_button_codes.h
        src/Honey/imgui/imgui_build.cpp
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
        src/Honey/debug/instrumentor.h
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
        src/Honey/renderer/renderer_2d.cpp
        src/Honey/renderer/renderer_2d.h
        src/Honey/renderer/renderer_3d.h
        src/Honey/renderer/renderer_3d.cpp
        src/Honey/renderer/sub_texture_2d.h
        src/Honey/renderer/sub_texture_2d.cpp
        src/Honey/renderer/framebuffer.h
        src/Honey/renderer/framebuffer.cpp
        src/platform/opengl/opengl_framebuffer.h
        src/platform/opengl/opengl_framebuffer.cpp
        src/platform/linux/linux_window.cpp
        src/platform/linux/linux_input.cpp
        src/platform/linux/linux_window.h
        src/Honey/scene/scene.h
        src/Honey/scene/scene.cpp
        src/Honey/scene/components.h
        src/Honey/scene/entity.h
        src/Honey/scene/entity.cpp
        src/Honey/scene/scriptable_entity.h
        src/Honey/scene/components.cpp
        src/Honey/scene/scene_serializer.h
        src/Honey/scene/scene_serializer.cpp
        src/Honey/utils/platform_utils.h
        src/platform/linux/linux_platform_utils.cpp
        src/platform/windows/windows_platform_utils.cpp
        src/platform/macos/macos_platform_utils.cpp
        src/Honey/math/math.h
        src/Honey/math/math.cpp
        src/Honey/renderer/editor_camera.h
        src/Honey/renderer/editor_camera.cpp

)

# Add the Objective‑C++ bridge only on macOS
if(APPLE)
 target_sources(engine PRIVATE
         src/platform/macos/macos_file_dialog_bridge.mm
 )
 set_source_files_properties(
         src/platform/macos/macos_file_dialog_bridge.mm
         PROPERTIES
         SKIP_PRECOMPILE_HEADERS ON
         # Optionally enable ARC:
         # COMPILE_FLAGS "-fobjc-arc"
 )

endif()

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
        $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_SOURCE_DIR}/src/hnpch.h">
)


target_compile_definitions(engine PRIVATE
        STBI_NO_SIMD
)

# Make the engine header accessible to other targets
target_include_directories(engine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb_image
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/entt/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(engine
        PUBLIC
        $<$<CONFIG:Debug,RelWithDebInfo>:BUILD_DEBUG>
        $<$<CONFIG:Release>:BUILD_RELEASE NDEBUG>
)

if(MSVC)
 target_compile_options(engine
         PRIVATE
         $<$<CONFIG:Release>:/O2 /Ot /Ob2>      # high optimisation
         $<$<CONFIG:Debug>:/Od /RTC1>           # no optimisation + run-time checks
 )
 target_link_options(engine PRIVATE
         $<$<CONFIG:Release>:/LTCG>                 # link-time optimisation (LTO)
 )
else() # GCC / Clang / AppleClang
 target_compile_options(engine
         PRIVATE
         $<$<CONFIG:Release>:-O3 -march=native> # high optimisation
         $<$<CONFIG:Debug>:-O0 -g>              # debug friendly
 )
 # Try to enable LTO for Release when the toolchain supports it
 include(CheckIPOSupported)
 check_ipo_supported(RESULT IPO_OK OUTPUT IPO_ERR)
 if(IPO_OK)
  set_property(TARGET engine PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
 endif()
endif()



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

target_compile_definitions(engine PUBLIC
        GLM_FORCE_SILENT_WARNINGS
        GLM_FORCE_INLINE
        GLM_FORCE_INTRINSICS
        $<$<CONFIG:Release>:GLM_FORCE_SIMD_AVX2>
)


add_subdirectory(vendor/GLAD)
add_subdirectory(vendor/imgui-wrapper)


#add_subdirectory(vendor/glm)

target_link_libraries(engine PRIVATE
        spdlog_header_only
        glfw
        glad
        glm
        PUBLIC
        imgui
        yaml-cpp::yaml-cpp
)

if (APPLE)
 #   find and link OpenGL.framework
 find_library(OPENGL_FRAMEWORK OpenGL)
 if(NOT OPENGL_FRAMEWORK)
  message(FATAL_ERROR "Could not find OpenGL.framework")
 endif()
 target_link_libraries(engine PRIVATE ${OPENGL_FRAMEWORK})

 # Link Cocoa umbrella framework (provides AppKit for NSOpenPanel/NSSavePanel)
 find_library(COCOA_FRAMEWORK Cocoa)
 if(NOT COCOA_FRAMEWORK)
  message(FATAL_ERROR "Could not find Cocoa.framework")
 endif()
 target_link_libraries(engine PRIVATE ${COCOA_FRAMEWORK})

elseif (WIN32)
 target_link_libraries(engine PRIVATE opengl32)     # native .lib
elseif (UNIX)        # Linux / BSD
 find_package(OpenGL REQUIRED)
 target_link_libraries(engine PRIVATE OpenGL::GL)
 target_link_libraries(engine PRIVATE rt)

 find_package(X11 REQUIRED)
 target_link_libraries(engine PRIVATE ${X11_LIBRARIES})
 target_include_directories(engine PRIVATE ${X11_INCLUDE_DIR})

 # Define GLFW_EXPOSE_NATIVE_X11 for X11 integration
 target_compile_definitions(engine PRIVATE GLFW_EXPOSE_NATIVE_X11)

 find_package(PkgConfig REQUIRED)
 pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
 target_link_libraries(engine PRIVATE ${GTK3_LIBRARIES})
 target_include_directories(engine PRIVATE ${GTK3_INCLUDE_DIRS})
 target_compile_options(engine PRIVATE ${GTK3_CFLAGS_OTHER})
endif()