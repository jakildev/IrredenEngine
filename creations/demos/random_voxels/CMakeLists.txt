set(IR_RANDOM_VOXELS_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_RANDOM_VOXELS_RUNTIME_DIR})
add_executable(IRRandomVoxels main.cpp)
target_link_libraries(IRRandomVoxels PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRRandomVoxels SCRIPTS config.lua)

add_custom_target(IRRandomVoxelsRun
    COMMAND $<TARGET_FILE:IRRandomVoxels>
    DEPENDS IRRandomVoxels IRRandomVoxelsAssets
    WORKING_DIRECTORY ${IR_RANDOM_VOXELS_RUNTIME_DIR}
    USES_TERMINAL
)
