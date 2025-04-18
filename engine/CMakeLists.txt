#engine/CMakeLists.txt

 # Create a shared library named "engine"
 add_library(engine SHARED
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
 )

target_precompile_headers(engine
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/hnpch.h>
)

 # Make the engine header accessible to other targets
 target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
 target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Platform‑specific compile definitions
if (WIN32)
 message(STATUS "Configuring for Windows")
 target_compile_definitions(engine PUBLIC
         HN_PLATFORM_WINDOWS
         HN_BUILD_DLL
 )
elseif (APPLE)
 message(STATUS "Configuring for macOS")
 target_compile_definitions(engine PUBLIC
         HN_PLATFORM_MACOS
         HN_BUILD_DYLIB    # or HN_BUILD_SHARED if you prefer
 )
elseif (UNIX)
 message(STATUS "Configuring for Linux/Unix")
 target_compile_definitions(engine PUBLIC
         HN_PLATFORM_LINUX
         HN_BUILD_SHARED
 )
else()
 message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

target_link_libraries(engine PRIVATE spdlog_header_only)

add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:engine>
        $<TARGET_FILE_DIR:application>
        COMMENT "Copying engine DLL to the application binary directory"
)