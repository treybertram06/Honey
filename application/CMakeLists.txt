# application/CMakeLists.txt

# Create an executable
add_executable(application
        src/application.cpp
)

target_precompile_headers(application
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/engine/src/hnpch.h>
)

# Link the game executable with the engine library - just once
target_link_libraries(application PUBLIC engine)

# Add explicit include directory
target_include_directories(application PRIVATE
        ${CMAKE_SOURCE_DIR}/engine/src
)


