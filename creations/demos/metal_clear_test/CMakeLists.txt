set(IR_MCT_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_MCT_RUNTIME_DIR})
add_executable(IRMetalClearTest main.cpp)
target_link_libraries(IRMetalClearTest PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRMetalClearTest SCRIPTS config.lua)

add_custom_target(IRMetalClearTestRun
    COMMAND $<TARGET_FILE:IRMetalClearTest>
    DEPENDS IRMetalClearTest IRMetalClearTestAssets
    WORKING_DIRECTORY ${IR_MCT_RUNTIME_DIR}
    USES_TERMINAL
)
