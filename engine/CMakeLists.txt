#engine/CMakeLists.txt

 # Create a shared library named "engine"
 add_library(engine SHARED
         src/Core/engine.cpp
         src/Core/engine.h
         src/Core/core.h
         src/Core/entry_point.h
 )

 # Make the engine header accessible to other targets
 target_include_directories(engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

 # On Windows, if you need to handle DLL export/import, you can define a macro (see below)
target_compile_definitions(engine PUBLIC EG_PLATFORM_WINDOWS EG_BUILD_DLL)
