cmake_minimum_required(VERSION 3.30)
project(game_engine)

set(CMAKE_CXX_STANDARD 20)


add_subdirectory(engine)
add_subdirectory(application)

target_include_directories(application PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)


# Create an imported target for spdlog
add_library(spdlog STATIC IMPORTED)
set_target_properties(spdlog PROPERTIES
        IMPORTED_LOCATION "${CMAKE_SOURCE_DIR}/vendor/spdlog/libspdlog.a"
        INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/vendor/spdlog/include"
)

target_link_libraries(engine PRIVATE spdlog)

set_target_properties(engine PROPERTIES LINK_FLAGS "-Wl,-subsystem,console")