# application/CMakeLists.txt

# Create an executable
add_executable(application
        src/application.cpp
)

target_precompile_headers(application
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/engine/src/hnpch.h>
)

# Link the game executable with the engine library.
target_link_libraries(application PRIVATE engine)
