set(IR_DAY_CYCLE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_DAY_CYCLE_RUNTIME_DIR})
add_executable(IRDayCycle main.cpp)
target_link_libraries(IRDayCycle PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRDayCycle
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRDayCycle> $<TARGET_RUNTIME_DLLS:IRDayCycle>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_DAY_CYCLE_SCRIPTS_DIR ${IR_DAY_CYCLE_RUNTIME_DIR}/scripts)
set(IR_DAY_CYCLE_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_DAY_CYCLE_CONFIG_LUA_DST ${IR_DAY_CYCLE_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_DAY_CYCLE_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_DAY_CYCLE_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_DAY_CYCLE_CONFIG_LUA_SRC} ${IR_DAY_CYCLE_CONFIG_LUA_DST}
    DEPENDS ${IR_DAY_CYCLE_CONFIG_LUA_SRC}
    COMMENT "Syncing day_cycle config.lua"
)

add_custom_target(IRDayCycleAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_DAY_CYCLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_DAY_CYCLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_DAY_CYCLE_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_DAY_CYCLE_CONFIG_LUA_DST}
)

add_dependencies(IRDayCycle IRDayCycleAssets)

add_custom_target(IRDayCycleRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_DAY_CYCLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_DAY_CYCLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_DAY_CYCLE_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_DAY_CYCLE_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRDayCycle>
    DEPENDS IRDayCycle
    WORKING_DIRECTORY ${IR_DAY_CYCLE_RUNTIME_DIR}
    USES_TERMINAL
)
