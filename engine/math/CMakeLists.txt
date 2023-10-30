add_library(IrredenEngineMath STATIC
    include/irreden/ir_math.hpp
    src/ir_math.cpp
)

target_compile_options(
    IrredenEngineMath
    PUBLIC
    -O3
)

# GLM
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG master
)
FetchContent_MakeAvailable(glm)

target_include_directories(
    IrredenEngineMath PUBLIC
    ${glm_SOURCE_DIR}
)

target_include_directories(
    IrredenEngineMath PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)