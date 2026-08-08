set(IR_REPOSITION_STRESS_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_REPOSITION_STRESS_RUNTIME_DIR})
add_executable(IRRepositionStress main.cpp)
target_link_libraries(IRRepositionStress PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRRepositionStress SCRIPTS config.lua)

# Build + launch from the exe directory (assets land via IRRepositionStressAssets).
add_custom_target(IRRepositionStressRun
    COMMAND $<TARGET_FILE:IRRepositionStress>
    DEPENDS IRRepositionStress IRRepositionStressAssets
    WORKING_DIRECTORY ${IR_REPOSITION_STRESS_RUNTIME_DIR}
    USES_TERMINAL
)
