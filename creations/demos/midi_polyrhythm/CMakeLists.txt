set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(IRMidiPolyrhythm
    main_lua.cpp
    lua_bindings.cpp
)
target_link_libraries(IRMidiPolyrhythm PUBLIC IrredenEngine)

# Lua scripts live in a subdirectory named after the target.
# The executable locates them at runtime via its own name (argv[0] stem).
set(IR_POLY_SCRIPTS_DIR ${CMAKE_BINARY_DIR}/IRMidiPolyrhythm)
set(IR_POLY_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Top-level scripts (engine expects main.lua and config.lua here)
set(IR_POLY_TOP_SCRIPTS main.lua config.lua)

set(IR_POLY_LUA_OUTPUTS "")
foreach(script ${IR_POLY_TOP_SCRIPTS})
    set(src ${IR_POLY_SRC_DIR}/${script})
    set(dst ${IR_POLY_SCRIPTS_DIR}/${script})
    add_custom_command(
        OUTPUT ${dst}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_POLY_SCRIPTS_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
        DEPENDS ${src}
        COMMENT "Syncing ${script}"
    )
    list(APPEND IR_POLY_LUA_OUTPUTS ${dst})
endforeach()

# Module scripts live in scripts/ and are copied as a directory
set(IR_POLY_SCRIPTS_STAMP ${CMAKE_BINARY_DIR}/stamp_poly_scripts)
file(GLOB IR_POLY_MODULE_SCRIPTS ${IR_POLY_SRC_DIR}/scripts/*.lua)
add_custom_command(
    OUTPUT ${IR_POLY_SCRIPTS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_POLY_SCRIPTS_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_POLY_SRC_DIR}/scripts ${IR_POLY_SCRIPTS_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E touch ${IR_POLY_SCRIPTS_STAMP}
    DEPENDS ${IR_POLY_MODULE_SCRIPTS}
    COMMENT "Syncing midi_polyrhythm scripts/"
)
list(APPEND IR_POLY_LUA_OUTPUTS ${IR_POLY_SCRIPTS_STAMP})

add_custom_target(IRMidiPolyrhythmAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${CMAKE_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${CMAKE_BINARY_DIR}/data
    DEPENDS ${IR_POLY_LUA_OUTPUTS}
    COMMENT "Syncing engine data and palette files"
)

add_dependencies(IRMidiPolyrhythm IRMidiPolyrhythmAssets)

# Run target: always copies latest scripts before launching
add_custom_target(IRMidiPolyrhythmRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${CMAKE_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${CMAKE_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_POLY_SCRIPTS_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_POLY_SRC_DIR}/main.lua ${IR_POLY_SCRIPTS_DIR}/main.lua
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_POLY_SRC_DIR}/config.lua ${IR_POLY_SCRIPTS_DIR}/config.lua
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_POLY_SRC_DIR}/scripts ${IR_POLY_SCRIPTS_DIR}/scripts
    COMMAND $<TARGET_FILE:IRMidiPolyrhythm>
    DEPENDS IRMidiPolyrhythm
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
)
