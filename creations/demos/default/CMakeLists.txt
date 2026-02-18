set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(IRCreationDefault
    main_lua.cpp
    lua_bindings.cpp
)
# add_executable(IRCreationDefault main.cpp)
target_link_libraries(IRCreationDefault PUBLIC IrredenEngine)

set(IR_DEFAULT_MAIN_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.lua)
set(IR_DEFAULT_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_DEFAULT_MAIN_LUA_DST ${PROJECT_BINARY_DIR}/main.lua)
set(IR_DEFAULT_CONFIG_LUA_DST ${PROJECT_BINARY_DIR}/config.lua)

# Track script files as build dependencies so normal UI build/run workflows
# refresh Lua assets without requiring a C++ recompile.
add_custom_command(
    OUTPUT ${IR_DEFAULT_MAIN_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IR_DEFAULT_MAIN_LUA_SRC} ${IR_DEFAULT_MAIN_LUA_DST}
    DEPENDS ${IR_DEFAULT_MAIN_LUA_SRC}
    COMMENT "Syncing default demo main.lua"
)

add_custom_command(
    OUTPUT ${IR_DEFAULT_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IR_DEFAULT_CONFIG_LUA_SRC} ${IR_DEFAULT_CONFIG_LUA_DST}
    DEPENDS ${IR_DEFAULT_CONFIG_LUA_SRC}
    COMMENT "Syncing default demo config.lua"
)

add_custom_target(IRCreationDefaultAssets
    DEPENDS
        ${IR_DEFAULT_MAIN_LUA_DST}
        ${IR_DEFAULT_CONFIG_LUA_DST}
)

add_dependencies(IRCreationDefault IRCreationDefaultAssets)

# Run helper target:
# - Copies the latest Lua/config files every run
# - Launches the creation executable from the build directory
# This lets script edits be picked up without rebuilding C++.
add_custom_target(IRCreationDefaultRun
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/main.lua ${PROJECT_BINARY_DIR}/main.lua
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${PROJECT_BINARY_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRCreationDefault>
    DEPENDS IRCreationDefault
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    USES_TERMINAL
)