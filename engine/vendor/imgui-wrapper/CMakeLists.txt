project(imgui)

add_library(${PROJECT_NAME} STATIC
        ../imgui/imgui.cpp
        ../imgui/imgui_demo.cpp
        ../imgui/imgui_draw.cpp
        ../imgui/imgui_tables.cpp
        ../imgui/imgui_widgets.cpp
        ../imgui/backends/imgui_impl_glfw.cpp
        ../imgui/backends/imgui_impl_opengl3.cpp
        ../imgui/backends/imgui_impl_vulkan.cpp

)

target_include_directories(${PROJECT_NAME}
        PUBLIC
        ../imgui
        ../imgui/backends
)

# ImGui requires these definitions for the OpenGL loader
target_compile_definitions(${PROJECT_NAME}
        PUBLIC
        IMGUI_IMPL_OPENGL_LOADER_GLAD
)

# Link against GLFW
target_link_libraries(${PROJECT_NAME}
        PUBLIC
        glfw
)