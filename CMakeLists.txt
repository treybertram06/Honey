cmake_minimum_required(VERSION 3.30)
project(game_engine)

set(CMAKE_CXX_STANDARD 20)



add_subdirectory(engine)
add_subdirectory(application)

target_include_directories(application PRIVATE ${CMAKE_SOURCE_DIR}/engine/src)


#add_executable(game_engine application/main.cpp)
