# application/CMakeLists.txt

add_executable(application
        src/application.cpp
        src/application_2d.h
        src/application_2d.cpp
)

# 1 ─ ensure engine headers are visible
target_link_libraries(application PRIVATE
        engine
        glfw
        glm
        glad
        #imgui
        #opengl32
)

target_precompile_headers(application PRIVATE
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/engine/src/hnpch.h>)


