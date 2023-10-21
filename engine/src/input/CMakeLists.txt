add_library(IrredenEngineInput STATIC
    ir_glfw_window.cpp
)

target_compile_options(
    IrredenEngineInput
    PUBLIC
    -O3
)

FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG master
)
FetchContent_MakeAvailable(glfw)
target_link_libraries(
    IrredenEngineInput PUBLIC
    glfw
    IrredenEngineProfiling
)
target_include_directories(
    IrredenEngineInput
    PUBLIC
    ${glfw_SOURCE_DIR}/include
)

add_custom_command(
    TARGET IrredenEngineInput
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${glfw_BINARY_DIR}/src/glfw3d.dll
        ${PROJECT_BINARY_DIR}/glfw3d.dll
)

# target_link_libraries()