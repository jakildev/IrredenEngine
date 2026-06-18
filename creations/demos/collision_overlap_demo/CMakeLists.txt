set(IR_COLLISION_OVERLAP_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_COLLISION_OVERLAP_DEMO_RUNTIME_DIR})
add_executable(IRCollisionOverlapDemo main_lua.cpp)
target_link_libraries(IRCollisionOverlapDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRCollisionOverlapDemo SCRIPTS main.lua config.lua)

add_custom_target(IRCollisionOverlapDemoRun
    COMMAND $<TARGET_FILE:IRCollisionOverlapDemo>
    DEPENDS IRCollisionOverlapDemo IRCollisionOverlapDemoAssets
    WORKING_DIRECTORY ${IR_COLLISION_OVERLAP_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
