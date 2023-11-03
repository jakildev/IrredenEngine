add_library(IrredenEngineWorld STATIC
    src/ir_world.cpp
)

target_link_libraries(IrredenEngineWorld PUBLIC
    IrredenEngineECS
    IrredenEngineProfiling
    IrredenEngineTime
    IrredenEngineAudio
    IrredenEngineInput
    IrredenEngineScripting
    IrredenEngineRendering
    IrredenEngineVideo
    IrredenEngineMath
    IrredenEngineCommand
)

target_include_directories(IrredenEngineWorld PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)