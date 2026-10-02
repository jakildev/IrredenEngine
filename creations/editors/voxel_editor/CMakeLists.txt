set(IR_VOXEL_EDITOR_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_VOXEL_EDITOR_RUNTIME_DIR})
add_executable(IRVoxelEditor main.cpp)
target_link_libraries(IRVoxelEditor PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs) plus the
# test/ module fixtures that `--module test/<name>` resolves against.
# See cmake/ir_functions.cmake.
irreden_bundle_assets(IRVoxelEditor
    SCRIPTS config.lua
    EXTRA_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/test test
)

# Build + launch from the exe directory (assets land via IRVoxelEditorAssets).
add_custom_target(IRVoxelEditorRun
    COMMAND $<TARGET_FILE:IRVoxelEditor>
    DEPENDS IRVoxelEditor IRVoxelEditorAssets
    WORKING_DIRECTORY ${IR_VOXEL_EDITOR_RUNTIME_DIR}
    USES_TERMINAL
)
