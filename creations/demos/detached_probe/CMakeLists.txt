set(IR_DETACHED_PROBE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_DETACHED_PROBE_RUNTIME_DIR})
add_executable(IRDetachedProbe main.cpp)
target_link_libraries(IRDetachedProbe PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRDetachedProbe SCRIPTS config.lua)

add_custom_target(IRDetachedProbeRun
    COMMAND $<TARGET_FILE:IRDetachedProbe>
    DEPENDS IRDetachedProbe IRDetachedProbeAssets
    WORKING_DIRECTORY ${IR_DETACHED_PROBE_RUNTIME_DIR}
    USES_TERMINAL
)
