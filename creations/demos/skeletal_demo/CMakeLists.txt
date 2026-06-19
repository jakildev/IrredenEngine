set(IR_SKELETAL_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_SKELETAL_DEMO_RUNTIME_DIR})
add_executable(IRSkeletalDemo main.cpp)
target_link_libraries(IRSkeletalDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRSkeletalDemo SCRIPTS config.lua)

add_custom_target(IRSkeletalDemoRun
    COMMAND $<TARGET_FILE:IRSkeletalDemo>
    DEPENDS IRSkeletalDemo IRSkeletalDemoAssets
    WORKING_DIRECTORY ${IR_SKELETAL_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
