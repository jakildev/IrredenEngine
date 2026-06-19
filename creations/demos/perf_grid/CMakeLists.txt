set(IR_PERF_GRID_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_PERF_GRID_RUNTIME_DIR})
add_executable(IRPerfGrid main.cpp)
target_link_libraries(IRPerfGrid PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRPerfGrid
    SCRIPTS config.lua
    EXTRA_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/configs configs
)

add_custom_target(IRPerfGridRun
    COMMAND $<TARGET_FILE:IRPerfGrid>
    DEPENDS IRPerfGrid IRPerfGridAssets
    WORKING_DIRECTORY ${IR_PERF_GRID_RUNTIME_DIR}
    USES_TERMINAL
)
