# application/CMakeLists.txt

add_executable(application
        src/application.cpp
        src/application_2d.h
        src/application_2d.cpp
        src/application_3d.h
        src/application_3d.cpp
)

set_target_properties(application PROPERTIES
        SKIP_PRECOMPILE_HEADERS ON
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

#target_precompile_headers(engine
 #       PRIVATE
        #$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/hnpch.h>
   #     "hnpch.h"
#)


# Add optimization flags to application target
if(MSVC)
    target_compile_options(application PRIVATE
            $<$<CONFIG:Release>:/O2 /Ot /Ob2 /GL>  # high optimisation + whole program optimization
            $<$<CONFIG:Debug>:/Od /RTC1>           # no optimisation + run-time checks
    )
    target_link_options(application PRIVATE
            $<$<CONFIG:Release>:/LTCG>             # link-time optimisation (LTO)
    )
else() # GCC / Clang / AppleClang
    target_compile_options(application PRIVATE
            #$<$<CONFIG:Release>:-O3 -march=native -flto> # high optimisation + LTO
            $<$<CONFIG:Release>:-O3 -march=native> # high optimisation
            $<$<CONFIG:Debug>:-O0 -g>                     # debug friendly
    )
endif()
