set(IR_LOWRES_PAN_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LOWRES_PAN_RUNTIME_DIR})
add_executable(IRLowresPan main.cpp)
target_link_libraries(IRLowresPan PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRLowresPan SCRIPTS config.lua)

add_custom_target(IRLowresPanRun
    COMMAND $<TARGET_FILE:IRLowresPan>
    DEPENDS IRLowresPan IRLowresPanAssets
    WORKING_DIRECTORY ${IR_LOWRES_PAN_RUNTIME_DIR}
    USES_TERMINAL
)
