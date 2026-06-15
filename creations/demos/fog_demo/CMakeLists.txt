set(IR_FOG_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_FOG_DEMO_RUNTIME_DIR})
add_executable(IRFogDemo main.cpp)
target_link_libraries(IRFogDemo PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRFogDemo
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRFogDemo> $<TARGET_RUNTIME_DLLS:IRFogDemo>
        COMMAND_EXPAND_LISTS
    )
endif()

# The demo is C++-only, but the engine's pre-init pass loads config.lua from
# the exe-relative scripts/ dir and aborts if it is missing, so a minimal
# config (window size + screenshot output dir) ships alongside the binary.
set(IR_FOG_DEMO_SCRIPTS_DIR ${IR_FOG_DEMO_RUNTIME_DIR}/scripts)
set(IR_FOG_DEMO_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_FOG_DEMO_CONFIG_LUA_DST ${IR_FOG_DEMO_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_FOG_DEMO_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_FOG_DEMO_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_FOG_DEMO_CONFIG_LUA_SRC} ${IR_FOG_DEMO_CONFIG_LUA_DST}
    DEPENDS ${IR_FOG_DEMO_CONFIG_LUA_SRC}
    COMMENT "Syncing fog_demo config.lua"
)

add_custom_target(IRFogDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_FOG_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_FOG_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_FOG_DEMO_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_FOG_DEMO_CONFIG_LUA_DST}
)

add_dependencies(IRFogDemo IRFogDemoAssets)

add_custom_target(IRFogDemoRun
    COMMAND $<TARGET_FILE:IRFogDemo>
    DEPENDS IRFogDemo
    WORKING_DIRECTORY ${IR_FOG_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
