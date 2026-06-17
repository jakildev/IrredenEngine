set(IR_CANVAS_STRESS_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_CANVAS_STRESS_RUNTIME_DIR})
add_executable(IRCanvasStress main.cpp)
target_link_libraries(IRCanvasStress PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRCanvasStress SCRIPTS config.lua)

add_custom_target(IRCanvasStressRun
    COMMAND $<TARGET_FILE:IRCanvasStress>
    DEPENDS IRCanvasStress IRCanvasStressAssets
    WORKING_DIRECTORY ${IR_CANVAS_STRESS_RUNTIME_DIR}
    USES_TERMINAL
)
