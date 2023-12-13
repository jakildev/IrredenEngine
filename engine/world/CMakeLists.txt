add_library(IrredenEngineWorld STATIC
    src/world.cpp
)

target_link_libraries(IrredenEngineWorld PUBLIC
    IrredenEngineECS
    IrredenEngineProfile
    IrredenEngineTime
    IrredenEngineAudio
    IrredenEngineInput
    IrredenEngineScripting
    IrredenEngineRendering
    IrredenEngineVideo
    IrredenEngineMath
    IrredenEngineCommand
    IrredenEngineAsset
)

target_include_directories(IrredenEngineWorld PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)