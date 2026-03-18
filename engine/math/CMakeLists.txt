add_library(IrredenEngineMath STATIC
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
    GIT_TAG 
)
FetchContent_MakeAvailable(glm)

target_link_libraries(
    IrredenEngineMath
    PUBLIC
    glm
)

target_include_directories(
    IrredenEngineMath PUBLIC
    ${glm_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# add_custom_command(
#     TARGET IrredenEngineMath
#     POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#         ${glm_BINARY_DIR}/glm/libglm_sharedd.dll
#         ${PROJECT_BINARY_DIR}/libglm_sharedd.dll
#     DEPENDS glm
# )