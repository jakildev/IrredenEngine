set(IR_MODIFIER_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_MODIFIER_DEMO_RUNTIME_DIR})
add_executable(IRModifierDemo main.cpp)
target_link_libraries(IRModifierDemo PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRModifierDemo
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRModifierDemo> $<TARGET_RUNTIME_DLLS:IRModifierDemo>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_MODIFIER_DEMO_SCRIPTS_DIR ${IR_MODIFIER_DEMO_RUNTIME_DIR}/scripts)
set(IR_MODIFIER_DEMO_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_MODIFIER_DEMO_CONFIG_LUA_DST ${IR_MODIFIER_DEMO_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_MODIFIER_DEMO_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_MODIFIER_DEMO_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_MODIFIER_DEMO_CONFIG_LUA_SRC} ${IR_MODIFIER_DEMO_CONFIG_LUA_DST}
    DEPENDS ${IR_MODIFIER_DEMO_CONFIG_LUA_SRC}
    COMMENT "Syncing modifier_demo config.lua"
)

add_custom_target(IRModifierDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_MODIFIER_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_MODIFIER_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_MODIFIER_DEMO_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_MODIFIER_DEMO_CONFIG_LUA_DST}
)

add_dependencies(IRModifierDemo IRModifierDemoAssets)

add_custom_target(IRModifierDemoRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_MODIFIER_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_MODIFIER_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_MODIFIER_DEMO_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_MODIFIER_DEMO_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRModifierDemo>
    DEPENDS IRModifierDemo
    WORKING_DIRECTORY ${IR_MODIFIER_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
