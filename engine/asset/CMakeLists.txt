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

# nlohmann/json — header-only, used privately for .txl.json sidecar I/O
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

target_link_libraries(
    IrredenEngineAsset
    PUBLIC
    IrredenEngineMath
    IrredenEngineProfile
    IrredenEngineUtility
    PRIVATE
    nlohmann_json::nlohmann_json
)

target_include_directories(
    IrredenEngineAsset PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)