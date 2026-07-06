add_library(IrredenEngineWorld STATIC
    src/world.cpp
    src/chunk_residency.cpp
    src/chunk_persistence.cpp
    src/world_snapshot.cpp
)

target_link_libraries(IrredenEngineWorld PUBLIC
    IrredenEngineEntity
    IrredenEngineSystem
    IrredenEngineJob
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
