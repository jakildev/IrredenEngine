set(IR_LUA_WIDGETS_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LUA_WIDGETS_RUNTIME_DIR})
add_executable(IRLuaWidgetsDemo main_lua.cpp)
target_link_libraries(IRLuaWidgetsDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRLuaWidgetsDemo SCRIPTS main.lua config.lua)

add_custom_target(IRLuaWidgetsDemoRun
    COMMAND $<TARGET_FILE:IRLuaWidgetsDemo>
    DEPENDS IRLuaWidgetsDemo IRLuaWidgetsDemoAssets
    WORKING_DIRECTORY ${IR_LUA_WIDGETS_RUNTIME_DIR}
    USES_TERMINAL
)
