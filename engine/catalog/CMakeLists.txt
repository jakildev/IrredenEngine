add_library(IrredenEngineCatalog STATIC
    src/catalog.cpp
    src/asset_index.cpp
)

target_compile_options(
    IrredenEngineCatalog
    PUBLIC
    -O3
)

target_link_libraries(
    IrredenEngineCatalog
    PUBLIC
    IrredenEngineAsset
    IrredenEngineCommon
    IrredenEngineUtility
)

target_include_directories(
    IrredenEngineCatalog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
