set(IR_VOXEL_YAW_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_VOXEL_YAW_RUNTIME_DIR})
add_executable(IRVoxelYaw main.cpp)
target_link_libraries(IRVoxelYaw PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRVoxelYaw SCRIPTS config.lua)

add_custom_target(IRVoxelYawRun
    COMMAND $<TARGET_FILE:IRVoxelYaw>
    DEPENDS IRVoxelYaw IRVoxelYawAssets
    WORKING_DIRECTORY ${IR_VOXEL_YAW_RUNTIME_DIR}
    USES_TERMINAL
)
