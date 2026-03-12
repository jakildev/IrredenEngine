set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
add_executable(IRUnitMovement
    main_lua.cpp
    lua_bindings.cpp
)
target_link_libraries(IRUnitMovement PUBLIC IrredenEngine)

set(IR_UNIT_SCRIPTS_DIR ${CMAKE_BINARY_DIR}/IRUnitMovement)
set(IR_UNIT_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(IR_UNIT_TOP_SCRIPTS main.lua config.lua)

set(IR_UNIT_LUA_OUTPUTS "")
foreach(script ${IR_UNIT_TOP_SCRIPTS})
    set(src ${IR_UNIT_SRC_DIR}/${script})
    set(dst ${IR_UNIT_SCRIPTS_DIR}/${script})
    add_custom_command(
        OUTPUT ${dst}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_UNIT_SCRIPTS_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${src} ${dst}
        DEPENDS ${src}
        COMMENT "Syncing ${script}"
    )
    list(APPEND IR_UNIT_LUA_OUTPUTS ${dst})
endforeach()

set(IR_UNIT_SCRIPTS_STAMP ${IR_UNIT_SCRIPTS_DIR}/.stamp_unit_scripts)
file(GLOB IR_UNIT_MODULE_SCRIPTS CONFIGURE_DEPENDS
    ${IR_UNIT_SRC_DIR}/scripts/*.lua
)
add_custom_command(
    OUTPUT ${IR_UNIT_SCRIPTS_STAMP}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_UNIT_SCRIPTS_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_UNIT_SRC_DIR}/scripts ${IR_UNIT_SCRIPTS_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E touch ${IR_UNIT_SCRIPTS_STAMP}
    DEPENDS ${IR_UNIT_MODULE_SCRIPTS}
    COMMENT "Syncing unit_movement scripts/"
)
list(APPEND IR_UNIT_LUA_OUTPUTS ${IR_UNIT_SCRIPTS_STAMP})

add_custom_target(IRUnitMovementAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${CMAKE_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${CMAKE_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${CMAKE_BINARY_DIR}/shaders
    DEPENDS ${IR_UNIT_LUA_OUTPUTS}
    COMMENT "Syncing engine data and shaders"
)

add_dependencies(IRUnitMovement IRUnitMovementAssets)

add_custom_target(IRUnitMovementRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${CMAKE_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${CMAKE_BINARY_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_UNIT_SCRIPTS_DIR}/scripts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_UNIT_SRC_DIR}/main.lua ${IR_UNIT_SCRIPTS_DIR}/main.lua
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_UNIT_SRC_DIR}/config.lua ${IR_UNIT_SCRIPTS_DIR}/config.lua
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_UNIT_SRC_DIR}/scripts ${IR_UNIT_SCRIPTS_DIR}/scripts
    COMMAND $<TARGET_FILE:IRUnitMovement>
    DEPENDS IRUnitMovement
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
)
