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

if(WIN32)
    add_custom_command(
        TARGET IRLuaPerfGrid
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRLuaPerfGrid> $<TARGET_RUNTIME_DLLS:IRLuaPerfGrid>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_LUA_PERF_GRID_SCRIPTS_DIR ${IR_LUA_PERF_GRID_RUNTIME_DIR}/scripts)
set(IR_LUA_PERF_GRID_MAIN_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.lua)
set(IR_LUA_PERF_GRID_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_LUA_PERF_GRID_MAIN_LUA_DST ${IR_LUA_PERF_GRID_SCRIPTS_DIR}/main.lua)
set(IR_LUA_PERF_GRID_CONFIG_LUA_DST ${IR_LUA_PERF_GRID_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_LUA_PERF_GRID_MAIN_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_LUA_PERF_GRID_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_LUA_PERF_GRID_MAIN_LUA_SRC} ${IR_LUA_PERF_GRID_MAIN_LUA_DST}
    DEPENDS ${IR_LUA_PERF_GRID_MAIN_LUA_SRC}
    COMMENT "Syncing lua_perf_grid main.lua"
)

add_custom_command(
    OUTPUT ${IR_LUA_PERF_GRID_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_LUA_PERF_GRID_CONFIG_LUA_SRC} ${IR_LUA_PERF_GRID_CONFIG_LUA_DST}
    DEPENDS ${IR_LUA_PERF_GRID_CONFIG_LUA_SRC}
    COMMENT "Syncing lua_perf_grid config.lua"
)

add_custom_target(IRLuaPerfGridAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_LUA_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_LUA_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_LUA_PERF_GRID_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_LUA_PERF_GRID_MAIN_LUA_DST}
        ${IR_LUA_PERF_GRID_CONFIG_LUA_DST}
)

add_dependencies(IRLuaPerfGrid IRLuaPerfGridAssets)

add_custom_target(IRLuaPerfGridRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_LUA_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_LUA_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_LUA_PERF_GRID_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/main.lua ${IR_LUA_PERF_GRID_SCRIPTS_DIR}/main.lua
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_LUA_PERF_GRID_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRLuaPerfGrid>
    DEPENDS IRLuaPerfGrid
    WORKING_DIRECTORY ${IR_LUA_PERF_GRID_RUNTIME_DIR}
    USES_TERMINAL
)
