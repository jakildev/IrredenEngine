add_library(IrredenEngineWorld STATIC
    # include/irreden/world/ir_world.hpp
    # include/irreden/world/ir_constants.hpp

    src/ir_world.cpp
)

target_link_libraries(IrredenEngineWorld PUBLIC
    IrredenEngineProfiling
    IrredenEngineAudio
    IrredenEngineInput
    IrredenEngineScripting
    IrredenEngineRendering
    IrredenEngineVideo
    IrredenEngineMath
    IrredenEngineECS
    IrredenEngineTime
)

target_include_directories(IrredenEngineWorld PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)