# engine/CMakeLists.txt

cmake_minimum_required(VERSION 3.16)

if(APPLE)
 enable_language(OBJCXX)
endif()

option(ENABLE_ASSERTS "Enable HN_ASSERT / HN_CORE_ASSERT checks" ON)

# Engine is a static lib, but we still build some deps as static/shared; keep this ON
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ------------------------------------------------------------------------------
# MSVC Runtime: make engine + its vendored deps (shaderc, glslang, spirv-tools, etc.)
# use the SAME CRT as the final app (Debug=/MDd, Release=/MD).
#
# IMPORTANT: This is intentionally scoped to THIS subtree, so Honey_Editor does not
# need to mention shaderc or engine internals. Honey_Editor just links "engine".
# ------------------------------------------------------------------------------
function(hn_force_msvc_runtime target_name)
 if(NOT MSVC)
  return()
 endif()

 if(NOT TARGET ${target_name})
  return()
 endif()

 # If it's an ALIAS target, resolve it.
 get_target_property(_aliased ${target_name} ALIASED_TARGET)
 if(_aliased)
  set(target_name ${_aliased})
 endif()

 if(NOT TARGET ${target_name})
  return()
 endif()

 set_property(TARGET ${target_name} PROPERTY
         MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
 )
endfunction()


# ------------------------------------------------------------------------------
# Engine library
# ------------------------------------------------------------------------------
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
        src/Honey/scripting/type_proxies.h
        src/Honey/scripting/script_properties_loader.h
        src/Honey/scripting/script_properties_loader.cpp
        src/Honey/scripting/script_properties_writer.h
        src/Honey/scripting/script_properties_writer.cpp
        src/Honey/core/settings.h
        src/Honey/renderer/sprite.h
        src/Honey/renderer/sprite.cpp
        src/Honey/renderer/texture_cache.h
        src/Honey/audio/audio_system.h
        src/Honey/audio/audio_system.cpp
        src/platform/vulkan/vk_context.h
        src/platform/vulkan/vk_context.cpp
        src/platform/vulkan/vk_renderer_api.h
        src/platform/vulkan/vk_renderer_api.cpp
        src/platform/vulkan/vk_vertex_array.h
        src/platform/vulkan/vk_vertex_array.cpp
        src/platform/vulkan/vk_buffer.h
        src/platform/vulkan/vk_buffer.cpp
        src/platform/vulkan/vk_texture.h
        src/platform/vulkan/vk_texture.cpp
        src/platform/vulkan/vk_queue_lease.h
        src/platform/vulkan/vk_backend.h
        src/platform/vulkan/vk_backend.cpp
        src/Honey/renderer/pipeline_spec.h
        src/platform/vulkan/vk_pipeline.h
        src/platform/vulkan/vk_pipeline.cpp
        src/platform/vulkan/vk_framebuffer.h
        src/platform/vulkan/vk_framebuffer.cpp
        src/Honey/ui/imgui_utils.h
        src/Honey/renderer/texture_cache.cpp
        src/Honey/core/settings.cpp
        src/Honey/math/yaml_glm.h
        src/Honey/renderer/pipeline.h
        src/Honey/renderer/pipeline.cpp
        src/Honey/ui/notifications.h
        src/Honey/ui/notifications.cpp
        src/Honey/renderer/material.h
        src/Honey/renderer/mesh.h
        src/Honey/renderer/mesh.cpp
        src/Honey/loaders/gltf_loader.h
        src/Honey/loaders/gltf_loader.cpp
        src/platform/vulkan/vk_pipeline_wrapper.h
        src/platform/vulkan/vk_pipeline_cache_blob.h
        src/platform/vulkan/vk_pipeline_cache_blob.cpp
        src/Honey/renderer/pipeline_spec.cpp
        src/Honey/core/task_system.h
        src/Honey/core/task_system.cpp
        src/Honey/loaders/texture_async.h
)

# Apply MSVC runtime to engine itself
hn_force_msvc_runtime(engine)

# ------------------------------------------------------------------------------
# enkiTS (vendored)
# ------------------------------------------------------------------------------
set(HN_ENKITS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/enkiTS" CACHE PATH "Vendored enkiTS root")

add_library(enkiTS STATIC
        ${HN_ENKITS_DIR}/src/TaskScheduler.cpp
        # add other .cpps here if your copy of enkiTS has more
)

target_include_directories(enkiTS PUBLIC
        ${HN_ENKITS_DIR}/src
)

# Make sure enkiTS uses same CRT on MSVC
hn_force_msvc_runtime(enkiTS)

# Link engine against enkiTS
target_link_libraries(engine PRIVATE enkiTS)

# ------------------------------------------------------------------------------
# Lua (vendored) + sol2 (header-only)
# Assumes:
#   - Lua sources are in: ${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua
#   - sol2 headers are in: ${CMAKE_CURRENT_SOURCE_DIR}/vendor/sol2/include
#
# If your lua.c files live in a "src" subfolder, change ${HN_LUA_DIR}/lapi.c to
# ${HN_LUA_DIR}/src/lapi.c, etc.
# ------------------------------------------------------------------------------
set(HN_LUA_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/vendor/lua"  CACHE PATH "Vendored Lua source root")
set(HN_SOL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/sol2" CACHE PATH "Vendored sol2 root")

add_library(lua STATIC
        ${HN_LUA_DIR}/lapi.c
        ${HN_LUA_DIR}/lcode.c
        ${HN_LUA_DIR}/lctype.c
        ${HN_LUA_DIR}/ldebug.c
        ${HN_LUA_DIR}/ldo.c
        ${HN_LUA_DIR}/ldump.c
        ${HN_LUA_DIR}/lfunc.c
        ${HN_LUA_DIR}/lgc.c
        ${HN_LUA_DIR}/llex.c
        ${HN_LUA_DIR}/lmem.c
        ${HN_LUA_DIR}/lobject.c
        ${HN_LUA_DIR}/lopcodes.c
        ${HN_LUA_DIR}/lparser.c
        ${HN_LUA_DIR}/lstate.c
        ${HN_LUA_DIR}/lstring.c
        ${HN_LUA_DIR}/ltable.c
        ${HN_LUA_DIR}/ltm.c
        ${HN_LUA_DIR}/lundump.c
        ${HN_LUA_DIR}/lvm.c
        ${HN_LUA_DIR}/lzio.c
        ${HN_LUA_DIR}/lauxlib.c
        ${HN_LUA_DIR}/lbaselib.c
        ${HN_LUA_DIR}/lcorolib.c
        ${HN_LUA_DIR}/ldblib.c
        ${HN_LUA_DIR}/liolib.c
        ${HN_LUA_DIR}/lmathlib.c
        ${HN_LUA_DIR}/loslib.c
        ${HN_LUA_DIR}/lstrlib.c
        ${HN_LUA_DIR}/ltablib.c
        ${HN_LUA_DIR}/lutf8lib.c
        ${HN_LUA_DIR}/loadlib.c
        ${HN_LUA_DIR}/linit.c
)

target_include_directories(lua PUBLIC ${HN_LUA_DIR})

# Make sure Lua uses the same CRT on MSVC
hn_force_msvc_runtime(lua)

add_library(sol2 INTERFACE)
target_include_directories(sol2 INTERFACE ${HN_SOL2_DIR}/include)

# Link engine against Lua + sol2
target_link_libraries(engine PRIVATE lua sol2)

# ------------------------------------------------------------------------------
# Objective-C++ bridge only on macOS
# ------------------------------------------------------------------------------
if(APPLE)
 target_sources(engine PRIVATE src/platform/macos/macos_file_dialog_bridge.mm)
 set_source_files_properties(
         src/platform/macos/macos_file_dialog_bridge.mm
         PROPERTIES SKIP_PRECOMPILE_HEADERS ON
 )
endif()

# ------------------------------------------------------------------------------
# MSVC specifics
# ------------------------------------------------------------------------------
if(MSVC)
 target_compile_definitions(engine PRIVATE
         HN_PLATFORM_WINDOWS
         _CRT_SECURE_NO_WARNINGS
 )
endif()

# ------------------------------------------------------------------------------
# Platform compile definitions
# ------------------------------------------------------------------------------
if(WIN32)
 message(STATUS "Configuring for Windows")
 target_compile_definitions(engine PUBLIC HN_PLATFORM_WINDOWS GLFW_INCLUDE_NONE)
elseif(APPLE)
 message(STATUS "Configuring for macOS")
 target_compile_definitions(engine PUBLIC GL_SILENCE_DEPRECATION HN_PLATFORM_MACOS GLFW_INCLUDE_NONE)
elseif(UNIX)
 message(STATUS "Configuring for Linux/Unix")
 target_compile_definitions(engine PUBLIC HN_PLATFORM_LINUX GLFW_INCLUDE_NONE)
else()
 message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

if(ENABLE_ASSERTS)
 target_compile_definitions(engine PUBLIC HN_ENABLE_ASSERTS)
endif()

target_compile_definitions(engine PRIVATE
        STBI_NO_SIMD
)

# Public headers for consumers (Honey_Editor is a consumer of engine)
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

# ------------------------------------------------------------------------------
# Optimization flags
# ------------------------------------------------------------------------------
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

target_compile_definitions(engine PUBLIC
        GLM_FORCE_SILENT_WARNINGS
        GLM_FORCE_INLINE
        GLM_FORCE_INTRINSICS
        $<$<CONFIG:Release>:GLM_FORCE_SIMD_AVX2>
)

# ------------------------------------------------------------------------------
# Subprojects (needed early for include dirs / PCH correctness)
# ------------------------------------------------------------------------------
add_subdirectory(vendor/GLAD)
add_subdirectory(vendor/imgui-wrapper)

# ------------------------------------------------------------------------------
# SPIRV-Cross (MUST be before PCH so PCH sees include dirs)
# ------------------------------------------------------------------------------
set(SPIRV_CROSS_CLI OFF CACHE BOOL "" FORCE)
set(SPIRV_CROSS_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(vendor/spirv-cross)

if(TARGET spirv-cross-c)
 target_link_libraries(engine PRIVATE spirv-cross-c)
else()
 message(FATAL_ERROR "SPIRV-Cross target 'spirv-cross-c' not found. Check the submodule and its CMake targets.")
endif()

if(TARGET spirv-cross-core)
 target_link_libraries(engine PRIVATE spirv-cross-core)
endif()

target_include_directories(engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/spirv-cross/include
)

# ------------------------------------------------------------------------------
# Precompiled headers (AFTER all critical include dirs are set)
# ------------------------------------------------------------------------------
target_precompile_headers(engine PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:"${CMAKE_CURRENT_SOURCE_DIR}/src/hnpch.h">
)

# ------------------------------------------------------------------------------
# Shaderc (vendored) - DEBUG-friendly Windows setup (dynamic)
# Engine links shaderc_shared; the final EXE will copy runtime DLLs automatically.
# ------------------------------------------------------------------------------

# Make sure shaderc + its deps use the same CRT as the rest of the build on MSVC.
# This is directory-scoped (applies to vendor/shaderc subtree too).
if(MSVC)
 # Debug = /MDd, Release = /MD
 set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

# Point shaderc at your vendored third_party folders
set(SHADERC_THIRD_PARTY_ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor" CACHE STRING "" FORCE)
set(SHADERC_SPIRV_TOOLS_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/vendor/spirv-tools"   CACHE STRING "" FORCE)
set(SHADERC_SPIRV_HEADERS_DIR   "${CMAKE_CURRENT_SOURCE_DIR}/vendor/spirv-headers" CACHE STRING "" FORCE)
set(SHADERC_GLSLANG_DIR         "${CMAKE_CURRENT_SOURCE_DIR}/vendor/glslang"       CACHE STRING "" FORCE)

# Keep shaderc minimal
set(SHADERC_SKIP_TESTS        ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXAMPLES     ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_EXECUTABLES  ON  CACHE BOOL "" FORCE)
set(SHADERC_SKIP_INSTALL      ON  CACHE BOOL "" FORCE)

set(SHADERC_ENABLE_TESTS       OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_EXAMPLES    OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(SHADERC_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)

set(ENABLE_GLSLANG_BINARIES OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL  OFF CACHE BOOL "" FORCE)
set(SPIRV_HEADERS_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(SPIRV_TOOLS_ENABLE_INSTALL   OFF CACHE BOOL "" FORCE)

set(CMAKE_SKIP_INSTALL_RULES ON)
add_subdirectory(vendor/shaderc)

# Ensure <shaderc/shaderc.hpp> resolves even if vendored targets don’t propagate includes properly
target_include_directories(engine PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/vendor/shaderc/libshaderc/include"
)

# ------------------------------------------------------------------------------
# SoLoud (inline) with miniaudio backend
# ------------------------------------------------------------------------------
set(SOLOUD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/soloud)

file(GLOB SOLOUD_CORE
        ${SOLOUD_DIR}/src/core/*.cpp
        ${SOLOUD_DIR}/src/filter/*.cpp
)

# Only include the audio sources you actually need.
set(SOLOUD_AUDIO_SOURCES
        ${SOLOUD_DIR}/src/audiosource/wav/soloud_wav.cpp
        ${SOLOUD_DIR}/src/audiosource/wav/dr_impl.cpp
        ${SOLOUD_DIR}/src/audiosource/wav/stb_vorbis.c
        ${SOLOUD_DIR}/src/audiosource/wav/soloud_wavstream.cpp     # optional, for streaming/ogg
        # If you want speech too:
        ${SOLOUD_DIR}/src/audiosource/speech/soloud_speech.cpp
        ${SOLOUD_DIR}/src/audiosource/speech/resonator.cpp
        ${SOLOUD_DIR}/src/audiosource/speech/tts.cpp
        ${SOLOUD_DIR}/src/audiosource/speech/darray.cpp
        ${SOLOUD_DIR}/src/audiosource/speech/klatt.cpp
)

set(SOLOUD_BACKENDS
        ${SOLOUD_DIR}/src/backend/miniaudio/soloud_miniaudio.cpp
)

add_library(soloud STATIC
        ${SOLOUD_CORE}
        ${SOLOUD_AUDIO_SOURCES}
        ${SOLOUD_BACKENDS}
)

target_include_directories(soloud PUBLIC
        ${SOLOUD_DIR}/include
)

if (UNIX AND NOT APPLE)
   target_compile_definitions(soloud PUBLIC WITH_MINIAUDIO)
else ()
   error("SoLoud currently only supports the miniaudio backend on Linux.")
endif ()
# Make sure SoLoud uses the same CRT on MSVC
hn_force_msvc_runtime(soloud)

# tinygltf is header-only, but it depends on stb_image + stb_image_write
target_include_directories(engine PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/tinygltf
        ${CMAKE_CURRENT_SOURCE_DIR}/vendor/stb_image
)

# Prefer dynamic for Debug builds on Windows: simplest and avoids CRT static mismatch pain
if(TARGET shaderc_shared)
 target_link_libraries(engine PRIVATE shaderc_shared)
elseif(TARGET shaderc)
 # Some setups only provide static 'shaderc'
 target_link_libraries(engine PRIVATE shaderc)
else()
 message(FATAL_ERROR "Expected shaderc targets were not created (shaderc_shared or shaderc). Check vendor/shaderc CMake.")
endif()

# ------------------------------------------------------------------------------
# Engine dependencies
# ------------------------------------------------------------------------------
target_link_libraries(engine
        PRIVATE
        spdlog_header_only
        glfw
        glad
        glm
        soloud
        PUBLIC
        imgui
        yaml-cpp::yaml-cpp
)

# ------------------------------------------------------------------------------
# Platform graphics / window system libs
# ------------------------------------------------------------------------------
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

 execute_process(COMMAND brew --prefix
         OUTPUT_VARIABLE HOMEBREW_PREFIX
         OUTPUT_STRIP_TRAILING_WHITESPACE
         ERROR_QUIET
 )
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
 target_link_libraries(engine PRIVATE OpenGL::GL rt)

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

# ------------------------------------------------------------------------------
# Vulkan libs
# ------------------------------------------------------------------------------

if(APPLE)
 # Help CMake find the Vulkan SDK (MoltenVK) if installed via the official SDK
 if(DEFINED ENV{VULKAN_SDK})
  list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")
 endif()

elseif (UNIX AND NOT APPLE)
 find_package(Vulkan REQUIRED)
 target_link_libraries(engine PRIVATE Vulkan::Vulkan)

endif()

message(STATUS "Vulkan found: ${Vulkan_FOUND}")
if(DEFINED ENV{VULKAN_SDK})
 message(STATUS "Using Vulkan SDK: $ENV{VULKAN_SDK}")
endif()