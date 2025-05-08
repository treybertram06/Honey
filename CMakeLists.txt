######### Speed up build times
find_program(CCACHE_PROGRAM ccache)
if(CCACHE_PROGRAM)
    message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    set(CMAKE_C_COMPILER_LAUNCHER   ${CCACHE_PROGRAM})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${CCACHE_PROGRAM})
endif()
#################

# Force STATIC by default for all libraries
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Link libraries statically" FORCE)

cmake_minimum_required(VERSION 3.30)
project(game_engine)

set(CMAKE_CXX_STANDARD 20)

# GLFW ---------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)

# If using MSVC, force consistent runtime library settings for all dependencies
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/engine/vendor/GLFW)
add_subdirectory(engine)

add_subdirectory(${CMAKE_SOURCE_DIR}/engine/vendor/spdlog)
add_subdirectory(${CMAKE_SOURCE_DIR}/engine/vendor/glm)

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

if(MSVC)
    # Disable warnings about secure CRT functions
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)

    # Add multi-processor compilation
    add_compile_options(/MP)

    # Specify more modern C++ options for MSVC
    add_compile_options(/Zc:__cplusplus)
endif()
