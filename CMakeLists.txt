cmake_minimum_required(VERSION 3.30)
project(game_engine)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(engine)

add_subdirectory(${CMAKE_SOURCE_DIR}/engine/vendor/spdlog)

add_subdirectory(application)

target_include_directories(application PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)
target_include_directories(application PRIVATE ${CMAKE_SOURCE_DIR}/vendor)

target_link_libraries(application PRIVATE spdlog_header_only)

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