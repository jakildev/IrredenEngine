set(COMMON_SRC
    src/ir_glfw_window.cpp
    src/ir_window.cpp
)

# GLFW
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw
    GIT_TAG 3.3.10
)
FetchContent_MakeAvailable(glfw)


add_library(IrredenEngineWindow STATIC
    ${COMMON_SRC}

    # unused atm
    # ${PLATFORM_SRC}
)

target_compile_options(
    IrredenEngineWindow
    PUBLIC
    -O3
)

target_link_libraries(
    IrredenEngineWindow PUBLIC
    IrredenEngineProfile
    IrredenEngineMath
    IrredenEngineEntity
    IrredenEngineSystem

    glfw

    # Unused atm
    # ${PLATFORM_LIBS}
)
target_include_directories(
    IrredenEngineWindow PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

IR_copyDLL(
    IrredenEngineWindow
    glfw3d
    ${glfw_BINARY_DIR}/src
)