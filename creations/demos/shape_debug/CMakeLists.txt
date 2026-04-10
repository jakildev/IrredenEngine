set(IR_SHAPE_DEBUG_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_SHAPE_DEBUG_RUNTIME_DIR})
add_executable(IRShapeDebug main.cpp)
target_link_libraries(IRShapeDebug PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRShapeDebug
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRShapeDebug> $<TARGET_RUNTIME_DLLS:IRShapeDebug>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_SHAPE_DEBUG_SCRIPTS_DIR ${IR_SHAPE_DEBUG_RUNTIME_DIR}/scripts)
set(IR_SHAPE_DEBUG_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_SHAPE_DEBUG_CONFIG_LUA_DST ${IR_SHAPE_DEBUG_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_SHAPE_DEBUG_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_SHAPE_DEBUG_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_SHAPE_DEBUG_CONFIG_LUA_SRC} ${IR_SHAPE_DEBUG_CONFIG_LUA_DST}
    DEPENDS ${IR_SHAPE_DEBUG_CONFIG_LUA_SRC}
    COMMENT "Syncing shape_debug config.lua"
)

add_custom_target(IRShapeDebugAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_SHAPE_DEBUG_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_SHAPE_DEBUG_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_SHAPE_DEBUG_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_SHAPE_DEBUG_CONFIG_LUA_DST}
)

add_dependencies(IRShapeDebug IRShapeDebugAssets)

add_custom_target(IRShapeDebugRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_SHAPE_DEBUG_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_SHAPE_DEBUG_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_SHAPE_DEBUG_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_SHAPE_DEBUG_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRShapeDebug>
    DEPENDS IRShapeDebug
    WORKING_DIRECTORY ${IR_SHAPE_DEBUG_RUNTIME_DIR}
    USES_TERMINAL
)
