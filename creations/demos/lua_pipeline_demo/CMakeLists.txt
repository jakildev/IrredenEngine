set(IR_LUA_PIPELINE_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR})
add_executable(IRLuaPipelineDemo main_lua.cpp)
target_link_libraries(IRLuaPipelineDemo PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRLuaPipelineDemo
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRLuaPipelineDemo> $<TARGET_RUNTIME_DLLS:IRLuaPipelineDemo>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_LUA_PIPELINE_DEMO_SCRIPTS_DIR ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}/scripts)
set(IR_LUA_PIPELINE_DEMO_MAIN_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/main.lua)
set(IR_LUA_PIPELINE_DEMO_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_LUA_PIPELINE_DEMO_MAIN_LUA_DST ${IR_LUA_PIPELINE_DEMO_SCRIPTS_DIR}/main.lua)
set(IR_LUA_PIPELINE_DEMO_CONFIG_LUA_DST ${IR_LUA_PIPELINE_DEMO_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_LUA_PIPELINE_DEMO_MAIN_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_LUA_PIPELINE_DEMO_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_LUA_PIPELINE_DEMO_MAIN_LUA_SRC} ${IR_LUA_PIPELINE_DEMO_MAIN_LUA_DST}
    DEPENDS ${IR_LUA_PIPELINE_DEMO_MAIN_LUA_SRC}
    COMMENT "Syncing lua_pipeline_demo main.lua"
)

add_custom_command(
    OUTPUT ${IR_LUA_PIPELINE_DEMO_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_LUA_PIPELINE_DEMO_CONFIG_LUA_SRC} ${IR_LUA_PIPELINE_DEMO_CONFIG_LUA_DST}
    DEPENDS ${IR_LUA_PIPELINE_DEMO_CONFIG_LUA_SRC}
    COMMENT "Syncing lua_pipeline_demo config.lua"
)

add_custom_target(IRLuaPipelineDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_LUA_PIPELINE_DEMO_MAIN_LUA_DST}
        ${IR_LUA_PIPELINE_DEMO_CONFIG_LUA_DST}
)

add_dependencies(IRLuaPipelineDemo IRLuaPipelineDemoAssets)

add_custom_target(IRLuaPipelineDemoRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/main.lua ${IR_LUA_PIPELINE_DEMO_SCRIPTS_DIR}/main.lua
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_LUA_PIPELINE_DEMO_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRLuaPipelineDemo>
    DEPENDS IRLuaPipelineDemo
    WORKING_DIRECTORY ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
