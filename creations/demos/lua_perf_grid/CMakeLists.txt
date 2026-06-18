set(IR_LUA_PERF_GRID_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LUA_PERF_GRID_RUNTIME_DIR})

add_executable(IRLuaPerfGrid main_lua.cpp)
target_link_libraries(IRLuaPerfGrid PUBLIC IrredenEngine)

# main.lua hosts the canonical schema + wave-system body. The codegen tool
# parses it at build time and emits lua_perf_grid_codegen.hpp containing
# the C_LuaWaveState struct, Lua bindings, and (under CODEGEN default) a
# typed System<...> specialisation for LuaWaveTick. DEFAULT_MODE is left
# unset so the build follows -DIR_LUA_ECS_DEFAULT_MODE (default CODEGEN);
# flipping the cmake var to EVAL flips this demo to the LuaJIT-backed
# dev-iteration path with the same source tree.
irreden_lua_codegen(IRLuaPerfGrid
    SOURCES main.lua
    OUTPUT_HPP ${CMAKE_CURRENT_BINARY_DIR}/codegen/lua_perf_grid_codegen.hpp
)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRLuaPerfGrid SCRIPTS main.lua config.lua)

add_custom_target(IRLuaPerfGridRun
    COMMAND $<TARGET_FILE:IRLuaPerfGrid>
    DEPENDS IRLuaPerfGrid IRLuaPerfGridAssets
    WORKING_DIRECTORY ${IR_LUA_PERF_GRID_RUNTIME_DIR}
    USES_TERMINAL
)
