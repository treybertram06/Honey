# engine/vendor/glad/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(glad C)

# Build as a static library
add_library(glad STATIC
        src/glad.c
)

# Public include path so consumers (your engine) can see <glad/glad.h> and <KHR/khrplatform.h>
target_include_directories(glad
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# On Linux / Unix, link against dl for dynamic symbol loading
if(UNIX)
    target_link_libraries(glad
            PUBLIC
            ${CMAKE_DL_LIBS}
    )
endif()

# Optionally, if you want to compile with position‐independent code
# set_target_properties(glad PROPERTIES POSITION_INDEPENDENT_CODE ON)
