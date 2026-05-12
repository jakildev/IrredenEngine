add_library(IrredenEngineAsset STATIC
    src/ir_asset.cpp
    src/binary_io.cpp
    src/chunk_header.cpp
    src/name_table.cpp
    src/json_sidecar.cpp
    src/voxel_set_format.cpp
    src/rig_format.cpp
)

target_compile_options(
    IrredenEngineAsset
    PUBLIC
    -O3
)

target_link_libraries(
    IrredenEngineAsset
    PUBLIC
    IrredenEngineMath
    IrredenEngineProfile
    IrredenEngineUtility
)

target_include_directories(
    IrredenEngineAsset PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)