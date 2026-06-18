set(IR_LUA_OVERLAP_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LUA_OVERLAP_DEMO_RUNTIME_DIR})
add_executable(IRLuaOverlapDemo main_lua.cpp)
target_link_libraries(IRLuaOverlapDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRLuaOverlapDemo SCRIPTS main.lua config.lua)

add_custom_target(IRLuaOverlapDemoRun
    COMMAND $<TARGET_FILE:IRLuaOverlapDemo>
    DEPENDS IRLuaOverlapDemo IRLuaOverlapDemoAssets
    WORKING_DIRECTORY ${IR_LUA_OVERLAP_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
