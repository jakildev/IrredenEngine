set(IR_MODIFIER_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_MODIFIER_DEMO_RUNTIME_DIR})
add_executable(IRModifierDemo main.cpp)
target_link_libraries(IRModifierDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRModifierDemo SCRIPTS config.lua)

add_custom_target(IRModifierDemoRun
    COMMAND $<TARGET_FILE:IRModifierDemo>
    DEPENDS IRModifierDemo IRModifierDemoAssets
    WORKING_DIRECTORY ${IR_MODIFIER_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
