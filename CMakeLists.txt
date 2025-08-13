######### Speed up build times
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER   ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()

#set(CMAKE_UNITY_BUILD ON)
#################

# Force STATIC by default for all libraries
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Link libraries statically" FORCE)

cmake_minimum_required(VERSION 3.30)
project(game_engine)

set(CMAKE_CXX_STANDARD 20)

if(UNIX AND NOT APPLE)
# Force use of system wayland-scanner instead of custom KDE one
find_program(WAYLAND_SCANNER wayland-scanner PATHS /usr/bin NO_DEFAULT_PATH)
if(WAYLAND_SCANNER)
    set(WAYLAND_SCANNER_EXECUTABLE ${WAYLAND_SCANNER} CACHE FILEPATH "Path to wayland-scanner" FORCE)
    message(STATUS "Using system wayland-scanner: ${WAYLAND_SCANNER}")
else()
    message(WARNING "System wayland-scanner not found, install with: sudo pacman -S wayland")
endif()
endif()


# GLFW ---------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
if (UNIX)
    set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_X11 ON CACHE BOOL "" FORCE)
    message(STATUS "Forcing X11 backend for ImGui docking support")
endif ()


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/engine/vendor/spdlog)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/engine/vendor/glfw)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/engine/vendor/glm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/vendor/yaml-cpp)
add_subdirectory(engine)

target_link_libraries(engine PUBLIC
        spdlog_header_only
        glm
        yaml-cpp::yaml-cpp
)

set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)


target_compile_definitions(glm INTERFACE
        GLM_FORCE_SILENT_WARNINGS
        GLM_FORCE_INLINE
        GLM_FORCE_INTRINSICS
        # Enable AVX2 only for Release builds (guarded so Debug doesn’t become painful)
        $<$<CONFIG:Release>:GLM_FORCE_SIMD_AVX2>
)

target_compile_definitions(engine PUBLIC
        ASSET_ROOT="${CMAKE_SOURCE_DIR}/assets"
)


# Optional but often helpful: keep vectors naturally aligned (16 B) for faster loads/stores
#target_compile_definitions(glm INTERFACE GLM_FORCE_DEFAULT_ALIGNED_GENTYPES)



add_subdirectory(application)

target_include_directories(application PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_include_directories(application PRIVATE ${CMAKE_SOURCE_DIR}/vendor)

target_link_libraries(application PRIVATE spdlog_header_only glfw glm)

if(APPLE)
    # Option A: direct framework link
    target_link_libraries(engine PUBLIC "-framework Cocoa")

    # Option B: find it explicitly then link
    find_library(COCOA_FRAMEWORK Cocoa)
    if(NOT COCOA_FRAMEWORK)
        message(FATAL_ERROR "Could not find Cocoa.framework")
    endif()
    target_link_libraries(engine PUBLIC ${COCOA_FRAMEWORK})
endif()

function(set_fast_math_flags tgt)
    if(MSVC)
        target_compile_options(${tgt} PRIVATE
                $<$<CONFIG:Release>:/arch:AVX2 /fp:fast>
        )
    else()
        target_compile_options(${tgt} PRIVATE
                $<$<CONFIG:Release>:-mavx2 -mfma -ffp-contract=fast
                -fno-math-errno -funsafe-math-optimizations
                -fno-trapping-math>
        )
    endif()
endfunction()

set_fast_math_flags(engine)
set_fast_math_flags(application)

