set(IR_FOG_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_FOG_DEMO_RUNTIME_DIR})
add_executable(IRFogDemo main.cpp)
target_link_libraries(IRFogDemo PUBLIC IrredenEngine)

irreden_bundle_assets(
    IRFogDemo
    SCRIPTS config.lua scripts/fog_binding_cap_selftest.lua scripts/fog_binding_selftest.lua
)

add_custom_target(IRFogDemoRun
    COMMAND $<TARGET_FILE:IRFogDemo>
    DEPENDS IRFogDemo IRFogDemoAssets
    WORKING_DIRECTORY ${IR_FOG_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
