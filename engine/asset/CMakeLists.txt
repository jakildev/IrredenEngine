add_library(IrredenEngineAsset STATIC
    src/ir_asset.cpp
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
)

target_include_directories(
    IrredenEngineAsset PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)