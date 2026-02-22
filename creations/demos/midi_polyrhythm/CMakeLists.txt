set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(IRMidiPolyrhythm
    main_lua.cpp
    lua_bindings.cpp
)
target_link_libraries(IRMidiPolyrhythm PUBLIC IrredenEngine)

# Lua scripts live in a subdirectory named after the target.
# The executable locates them at runtime via its own name (argv[0] stem).
set(IR_POLY_SCRIPTS_DIR ${CMAKE_BINARY_DIR}/IRMidiPolyrhythm)
set(IR_POLY_MAIN_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.lua)
set(IR_POLY_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_POLY_MAIN_LUA_DST ${IR_POLY_SCRIPTS_DIR}/main.lua)
set(IR_POLY_CONFIG_LUA_DST ${IR_POLY_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_POLY_MAIN_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_POLY_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IR_POLY_MAIN_LUA_SRC} ${IR_POLY_MAIN_LUA_DST}
    DEPENDS ${IR_POLY_MAIN_LUA_SRC}
    COMMENT "Syncing midi_polyrhythm main.lua"
)

add_custom_command(
    OUTPUT ${IR_POLY_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IR_POLY_CONFIG_LUA_SRC} ${IR_POLY_CONFIG_LUA_DST}
    DEPENDS ${IR_POLY_CONFIG_LUA_SRC}
    COMMENT "Syncing midi_polyrhythm config.lua"
)

add_custom_target(IRMidiPolyrhythmAssets
    DEPENDS
        ${IR_POLY_MAIN_LUA_DST}
        ${IR_POLY_CONFIG_LUA_DST}
)

add_dependencies(IRMidiPolyrhythm IRMidiPolyrhythmAssets)

add_custom_target(IRMidiPolyrhythmRun
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/main.lua ${IR_POLY_SCRIPTS_DIR}/main.lua
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_POLY_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRMidiPolyrhythm>
    DEPENDS IRMidiPolyrhythm
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
)
