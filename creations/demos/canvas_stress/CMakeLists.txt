set(IR_CANVAS_STRESS_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_CANVAS_STRESS_RUNTIME_DIR})
add_executable(IRCanvasStress main.cpp)
target_link_libraries(IRCanvasStress PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRCanvasStress
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRCanvasStress> $<TARGET_RUNTIME_DLLS:IRCanvasStress>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_CANVAS_STRESS_SCRIPTS_DIR ${IR_CANVAS_STRESS_RUNTIME_DIR}/scripts)
set(IR_CANVAS_STRESS_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_CANVAS_STRESS_CONFIG_LUA_DST ${IR_CANVAS_STRESS_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_CANVAS_STRESS_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_CANVAS_STRESS_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_CANVAS_STRESS_CONFIG_LUA_SRC} ${IR_CANVAS_STRESS_CONFIG_LUA_DST}
    DEPENDS ${IR_CANVAS_STRESS_CONFIG_LUA_SRC}
    COMMENT "Syncing canvas_stress config.lua"
)

add_custom_target(IRCanvasStressAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_CANVAS_STRESS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_CANVAS_STRESS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_CANVAS_STRESS_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_CANVAS_STRESS_CONFIG_LUA_DST}
)

add_dependencies(IRCanvasStress IRCanvasStressAssets)

add_custom_target(IRCanvasStressRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_CANVAS_STRESS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_CANVAS_STRESS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_CANVAS_STRESS_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_CANVAS_STRESS_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRCanvasStress>
    DEPENDS IRCanvasStress
    WORKING_DIRECTORY ${IR_CANVAS_STRESS_RUNTIME_DIR}
    USES_TERMINAL
)
