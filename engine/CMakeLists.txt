# engine/CMakeLists.txt

if(APPLE)
 enable_language(OBJCXX)
endif()

option(ENABLE_ASSERTS "Enable HN_ASSERT / HN_CORE_ASSERT checks" ON)

set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(HN_MONO_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/mono-bin" CACHE PATH "Vendored Mono root")

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
        src/Honey/renderer/shader_compiler.h
        src/Honey/renderer/shader_cache.h
        src/Honey/core/timer.h
        src/Honey/renderer/shader_cache.cpp
        src/Honey/renderer/shader_compiler.cpp
        src/Honey/scene/script_registry.h
        src/Honey/core/uuid.h
        src/Honey/core/uuid.cpp
        src/Honey/scripting/script_engine.h
        src/Honey/scripting/script_engine.cpp
        src/Honey/scripting/script_glue.h
        src/Honey/scripting/script_glue.cpp
)

# ---------------- Mono (vendored, required) ----------------
function(hn_require_mono target)
 # Headers (required)
 set(_mono_inc "${HN_MONO_VENDOR_DIR}/include/mono-2.0")
 if(NOT EXISTS "${_mono_inc}")
  message(FATAL_ERROR "Vendored Mono headers not found: ${_mono_inc}")
 endif()
 target_include_directories(${target} PRIVATE "${_mono_inc}")

 # Pick platform runtime root
 if(WIN32)
  set(_mono_root "${HN_MONO_VENDOR_DIR}/windows/x64")
 elseif(APPLE)
  set(_mono_root "${HN_MONO_VENDOR_DIR}/macos/universal")
 else()
  set(_mono_root "${HN_MONO_VENDOR_DIR}/linux/x64")
 endif()

 if(NOT EXISTS "${_mono_root}")
  message(FATAL_ERROR "Vendored Mono runtime not found: ${_mono_root}")
 endif()

 # Expose runtime root to C++ so you can call mono_set_dirs(<root>/lib, <root>/etc)
 target_compile_definitions(${target} PRIVATE HN_MONO_ROOT=\"${_mono_root}\")

 if(WIN32)
  # Do NOT link Mono on Windows; dynamic-load mono-2.0-sgen.dll instead (works with MinGW + MSVC)
  if(NOT EXISTS "${_mono_root}/bin/mono-2.0-sgen.dll")
   message(FATAL_ERROR "Expected Mono DLL not found: ${_mono_root}/bin/mono-2.0-sgen.dll")
  endif()
  target_compile_definitions(${target} PRIVATE HN_MONO_DYNAMIC_LOAD)
 else()
  # macOS/Linux: link to the shared library in the bundle (simplest)
  find_library(MONO_LIB
          NAMES monosgen-2.0 mono-2.0-sgen mono-2.0
          PATHS "${_mono_root}/lib" "${_mono_root}"
          REQUIRED
  )
  target_link_libraries(${target} PRIVATE "${MONO_LIB}")

  # Linux runtime search path (so the exe finds the bundled .so)
  if(UNIX AND NOT APPLE)
   target_link_options(${target} PRIVATE "-Wl,-rpath,$ORIGIN")
  endif()
 endif()
endfunction()

hn_require_mono(engine)

# ---------------- Objective-C++ bridge only on macOS ----------------
if(APPLE)
 target_sources(engine PRIVATE src/platform/macos/macos_file_dialog_bridge.mm)
 set_source_files_properties(
         src/platform/macos/macos_file_dialog_bridge.mm
         PROPERTIES
         SKIP_PRECOMPILE_HEADERS ON
         # COMPILE_FLAGS "-fobjc-arc"
 )
endif()

# ---------------- MSVC specifics ----------------
if(MSVC)
 target_compile_definitions(engine PRIVATE
         HN_PLATFORM_WINDOWS
         _CRT_SECURE_NO_WARNINGS
 )
endif()

target_compile_definitions(engine PRIVATE
        $<$<PLATFORM_ID:Windows>:HN_PLATFORM_WINDOWS>
)

target_precompile_headers(engine PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_SOURCE_DIR}/src/hnpch.h">
)

target_compile_definitions(engine PRIVATE STBI_NO_SIMD)

# Public headers for consumers
target_include_directories(engine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb_image
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/entt/src
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_compile_definitions(engine PUBLIC
        $<$<CONFIG:Debug,RelWithDebInfo>:BUILD_DEBUG>
        $<$<CONFIG:Release>:BUILD_RELEASE NDEBUG>
)

if(MSVC)
 target_compile_options(engine PRIVATE
         $<$<CONFIG:Release>:/O2 /Ot /Ob2>
         $<$<CONFIG:Debug>:/Od /RTC1>
 )
 target_link_options(engine PRIVATE
         $<$<CONFIG:Release>:/LTCG>
 )
else()
 target_compile_options(engine PRIVATE
         $<$<CONFIG:Release>:-O3 -march=native>
         $<$<CONFIG:Debug>:-O0 -g>
 )
 include(CheckIPOSupported)
 check_ipo_supported(RESULT IPO_OK OUTPUT IPO_ERR)
 if(IPO_OK)
  set_property(TARGET engine PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
 endif()
endif()

# Platform-specific compile definitions
if(WIN32)
 message(STATUS "Configuring for Windows")
 target_compile_definitions(engine PUBLIC
         HN_PLATFORM_WINDOWS
         GLFW_INCLUDE_NONE
 )
elseif(APPLE)
 message(STATUS "Configuring for macOS")
 target_compile_definitions(engine PUBLIC
         GL_SILENCE_DEPRECATION
         HN_PLATFORM_MACOS
         GLFW_INCLUDE_NONE
 )
elseif(UNIX)
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

# ---------------- SPIRV-Cross (build from source on ALL platforms) ----------------
# Keep SPIRV-Cross lightweight for embedding use
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/spirv-cross)

# Link the C API wrapper (you include spirv_cross_c.h / spirv_cross_c.cpp uses this)
# Some SPIRV-Cross versions require core as well; linking both is safe.
if(TARGET spirv-cross-c)
 target_link_libraries(engine PRIVATE spirv-cross-c)
else()
 message(FATAL_ERROR "SPIRV-Cross target 'spirv-cross-c' not found. Check the submodule and its CMake targets.")
endif()

# If core target exists, link it too (harmless if spirv-cross-c already pulls it in, but helps older versions)
if(TARGET spirv-cross-core)
 target_link_libraries(engine PRIVATE spirv-cross-core)
endif()

# I don't understand this, in GPT 5.2 we trust...
# ---- Prevent vendored deps from generating install/export rules ----
set(CMAKE_SKIP_INSTALL_RULES ON CACHE BOOL "" FORCE)
set(CMAKE_SKIP_INSTALL_ALL_DEPENDENCY ON CACHE BOOL "" FORCE)

# SPIRV-Headers / SPIRV-Tools / glslang commonly honor these:
set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

# ShaderC

set(SHADERC_THIRD_PARTY_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor" CACHE STRING "" FORCE)

set(SHADERC_SPIRV_TOOLS_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/vendor/spirv-tools"   CACHE STRING "" FORCE)
set(SHADERC_SPIRV_HEADERS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/spirv-headers" CACHE STRING "" FORCE)
set(SHADERC_GLSLANG_DIR       "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glslang"       CACHE STRING "" FORCE)

# Keep shaderc minimal (no tests/examples/tools)
set(SHADERC_SKIP_TESTS ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXECUTABLES ON CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL ON CACHE BOOL "" FORCE)

# Back-compat with some shaderc forks/versions
set(SHADERC_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

add_subdirectory(vendor/shaderc)

if(TARGET shaderc_shared)
 target_link_libraries(engine PRIVATE shaderc_shared)
elseif(TARGET shaderc)
 target_link_libraries(engine PRIVATE shaderc)
else()
 message(FATAL_ERROR "Shaderc target not found. Ensure vendor/shaderc is added via add_subdirectory().")
endif()

target_link_libraries(engine
        PRIVATE
        spdlog_header_only
        glfw
        glad
        glm
        PUBLIC
        imgui
        yaml-cpp::yaml-cpp
)

target_include_directories(engine PRIVATE
        ${SHADERC_INCLUDE_DIRS}
)

if(APPLE)
 find_library(OPENGL_FRAMEWORK OpenGL)
 if(NOT OPENGL_FRAMEWORK)
  message(FATAL_ERROR "Could not find OpenGL.framework")
 endif()
 target_link_libraries(engine PRIVATE ${OPENGL_FRAMEWORK})

 find_library(COCOA_FRAMEWORK Cocoa)
 if(NOT COCOA_FRAMEWORK)
  message(FATAL_ERROR "Could not find Cocoa.framework")
 endif()
 target_link_libraries(engine PRIVATE ${COCOA_FRAMEWORK})

 execute_process(COMMAND brew --prefix OUTPUT_VARIABLE HOMEBREW_PREFIX OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
 if(HOMEBREW_PREFIX)
  set(_HN_RPATH_CANDIDATES "${HOMEBREW_PREFIX}/lib")
 else()
  set(_HN_RPATH_CANDIDATES "/opt/homebrew/lib" "/usr/local/lib")
 endif()

 foreach(_rp ${_HN_RPATH_CANDIDATES})
  if(EXISTS "${_rp}")
   target_link_options(engine PUBLIC "-Wl,-rpath,${_rp}")
  endif()
 endforeach()

elseif(WIN32)
 target_link_libraries(engine PRIVATE opengl32)

elseif(UNIX)
 find_package(OpenGL REQUIRED)
 target_link_libraries(engine PRIVATE OpenGL::GL)
 target_link_libraries(engine PRIVATE rt)

 find_package(X11 REQUIRED)
 target_link_libraries(engine PRIVATE ${X11_LIBRARIES})
 target_include_directories(engine PRIVATE ${X11_INCLUDE_DIR})
 target_compile_definitions(engine PRIVATE GLFW_EXPOSE_NATIVE_X11)

 find_package(PkgConfig REQUIRED)
 pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
 target_link_libraries(engine PRIVATE ${GTK3_LIBRARIES})
 target_include_directories(engine PRIVATE ${GTK3_INCLUDE_DIRS})
 target_compile_options(engine PRIVATE ${GTK3_CFLAGS_OTHER})
endif()
