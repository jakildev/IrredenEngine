add_library(IrredenEngineInput STATIC
    src/ir_input.cpp
    src/input_manager.cpp
)

target_compile_options(
    IrredenEngineInput
    PUBLIC
    -O3
)


target_include_directories(
    IrredenEngineInput
    PUBLIC
    ${glfw_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(
    IrredenEngineInput PUBLIC
    IrredenEngineProfile
    IrredenEngineMath
    IrredenEngineEntity
    IrredenEngineSystem
    IrredenEngineWindow
)
