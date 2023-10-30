add_library(IrredenEngineCommand STATIC
    # include/irreden/ir_command.hpp
    include/irreden/command/command_manager.hpp

    src/command_manager.cpp
)

target_compile_options(
    IrredenEngineRendering
    PUBLIC
    -O3
)

target_include_directories(
    IrredenEngineCommand PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # ${PROJECT_SOURCE_DIR}
)

target_link_libraries(
    IrredenEngineCommand PUBLIC
    IrredenEngineInput
    # IrredenEngineECS
    IrredenEngineAudio
)