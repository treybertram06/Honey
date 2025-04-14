# application/CMakeLists.txt

# Create an executable
add_executable(application
        src/application.cpp
)

# Link the game executable with the engine library.
target_link_libraries(application PRIVATE engine)
