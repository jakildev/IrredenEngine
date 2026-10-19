add_library(IrredenEngineWorld STATIC
    src/world.cpp
    src/chunk_residency.cpp
    src/chunk_persistence.cpp
    src/field_chunk_persistence.cpp
    src/world_snapshot.cpp
    src/world_snapshot_relations.cpp
    src/world_default_registry.cpp
    src/hand_off.cpp
    src/tool_spawn.cpp
    ../process_spawn.cpp
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
    IrredenEngineUtility
)

target_include_directories(IrredenEngineWorld PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_include_directories(IrredenEngineWorld PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)
