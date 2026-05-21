set(IR_PERF_GRID_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_PERF_GRID_RUNTIME_DIR})
add_executable(IRPerfGrid main.cpp)
target_link_libraries(IRPerfGrid PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRPerfGrid
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRPerfGrid> $<TARGET_RUNTIME_DLLS:IRPerfGrid>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_PERF_GRID_SCRIPTS_DIR ${IR_PERF_GRID_RUNTIME_DIR}/scripts)
set(IR_PERF_GRID_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_PERF_GRID_CONFIG_LUA_DST ${IR_PERF_GRID_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_PERF_GRID_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_PERF_GRID_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_PERF_GRID_CONFIG_LUA_SRC} ${IR_PERF_GRID_CONFIG_LUA_DST}
    DEPENDS ${IR_PERF_GRID_CONFIG_LUA_SRC}
    COMMENT "Syncing perf_grid config.lua"
)

add_custom_target(IRPerfGridAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_PERF_GRID_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/configs ${IR_PERF_GRID_RUNTIME_DIR}/configs
    DEPENDS
        ${IR_PERF_GRID_CONFIG_LUA_DST}
)

add_dependencies(IRPerfGrid IRPerfGridAssets)

add_custom_target(IRPerfGridRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_PERF_GRID_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_PERF_GRID_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_PERF_GRID_SCRIPTS_DIR}/config.lua
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/configs ${IR_PERF_GRID_RUNTIME_DIR}/configs
    COMMAND $<TARGET_FILE:IRPerfGrid>
    DEPENDS IRPerfGrid
    WORKING_DIRECTORY ${IR_PERF_GRID_RUNTIME_DIR}
    USES_TERMINAL
)
