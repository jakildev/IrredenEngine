add_library(IrredenEngineInput STATIC
    src/ir_input.cpp
    src/input_manager.cpp
)

target_compile_options(
    IrredenEngineInput
    PUBLIC
    -O3
)

target_link_libraries(
    IrredenEngineInput PUBLIC
    IrredenEngineProfile
    IrredenEngineMath
    IrredenEngineEntity
    IrredenEngineSystem
    IrredenEngineWindow

)
target_include_directories(
    IrredenEngineInput
    PUBLIC
    ${glfw_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

IR_copyDLL(
    IrredenEngineInput
    glfw3d
    ${glfw_BINARY_DIR}/src
)
