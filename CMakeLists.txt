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

# GLFW ---------------------------------------------------
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)


add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/engine/vendor/glfw)
add_subdirectory(engine)

add_subdirectory(${CMAKE_SOURCE_DIR}/engine/vendor/spdlog)

set(GLM_TEST_ENABLE OFF CACHE BOOL "" FORCE)
add_subdirectory(${CMAKE_SOURCE_DIR}/engine/vendor/glm)

target_compile_definitions(glm INTERFACE
        GLM_FORCE_INLINE
        GLM_FORCE_INTRINSICS
        # Enable AVX2 only for Release builds (guarded so Debug doesn’t become painful)
        $<$<CONFIG:Release>:GLM_FORCE_SIMD_AVX2>
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

