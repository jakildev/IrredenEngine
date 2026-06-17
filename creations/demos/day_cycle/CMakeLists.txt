set(IR_DAY_CYCLE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_DAY_CYCLE_RUNTIME_DIR})
add_executable(IRDayCycle main.cpp)
target_link_libraries(IRDayCycle PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRDayCycle SCRIPTS config.lua)

add_custom_target(IRDayCycleRun
    COMMAND $<TARGET_FILE:IRDayCycle>
    DEPENDS IRDayCycle IRDayCycleAssets
    WORKING_DIRECTORY ${IR_DAY_CYCLE_RUNTIME_DIR}
    USES_TERMINAL
)
