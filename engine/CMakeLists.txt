#engine/CMakeLists.txt

 # Create a shared library named "engine"
 add_library(engine SHARED
         src/Honey/engine.cpp
         src/Honey/engine.h
         src/Honey/core.h
         src/Honey/entry_point.h
         src/Honey/log.h
         src/Honey/log.cpp
 )



 # Make the engine header accessible to other targets
 target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

 # On Windows, if you need to handle DLL export/import, you can define a macro (see below)
target_compile_definitions(engine PUBLIC HN_PLATFORM_WINDOWS HN_BUILD_DLL)

target_link_libraries(engine PRIVATE spdlog_header_only)

add_custom_command(TARGET engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:engine>
        $<TARGET_FILE_DIR:application>
        COMMENT "Copying engine DLL to the application binary directory"
)