set(IR_SPRITE_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_SPRITE_DEMO_RUNTIME_DIR})
add_executable(IRSpriteDemo
    main.cpp
    lua_bindings.cpp
)
target_link_libraries(IRSpriteDemo PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRSpriteDemo
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRSpriteDemo> $<TARGET_RUNTIME_DLLS:IRSpriteDemo>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_SPRITE_DEMO_SCRIPTS_DIR ${IR_SPRITE_DEMO_RUNTIME_DIR}/scripts)
set(IR_SPRITE_DEMO_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_SPRITE_DEMO_CONFIG_LUA_DST ${IR_SPRITE_DEMO_SCRIPTS_DIR}/config.lua)
set(IR_SPRITE_DEMO_MAIN_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/scripts/main.lua)
set(IR_SPRITE_DEMO_MAIN_LUA_DST ${IR_SPRITE_DEMO_SCRIPTS_DIR}/main.lua)

add_custom_command(
    OUTPUT ${IR_SPRITE_DEMO_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_SPRITE_DEMO_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_SPRITE_DEMO_CONFIG_LUA_SRC} ${IR_SPRITE_DEMO_CONFIG_LUA_DST}
    DEPENDS ${IR_SPRITE_DEMO_CONFIG_LUA_SRC}
    COMMENT "Syncing sprite_demo config.lua"
)

add_custom_command(
    OUTPUT ${IR_SPRITE_DEMO_MAIN_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_SPRITE_DEMO_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_SPRITE_DEMO_MAIN_LUA_SRC} ${IR_SPRITE_DEMO_MAIN_LUA_DST}
    DEPENDS ${IR_SPRITE_DEMO_MAIN_LUA_SRC}
    COMMENT "Syncing sprite_demo main.lua"
)

set(IR_SPRITE_DEMO_ASSET_SRC ${PROJECT_SOURCE_DIR}/assets/demos/sprite_demo)
set(IR_SPRITE_DEMO_ASSET_DST ${IR_SPRITE_DEMO_RUNTIME_DIR}/assets/sprite_demo)

add_custom_target(IRSpriteDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_SPRITE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_SPRITE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_SPRITE_DEMO_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_SPRITE_DEMO_ASSET_SRC} ${IR_SPRITE_DEMO_ASSET_DST}
    DEPENDS
        ${IR_SPRITE_DEMO_CONFIG_LUA_DST}
        ${IR_SPRITE_DEMO_MAIN_LUA_DST}
)

add_dependencies(IRSpriteDemo IRSpriteDemoAssets)

add_custom_target(IRSpriteDemoRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_SPRITE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_SPRITE_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_SPRITE_DEMO_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_SPRITE_DEMO_ASSET_SRC} ${IR_SPRITE_DEMO_ASSET_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_SPRITE_DEMO_CONFIG_LUA_SRC} ${IR_SPRITE_DEMO_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_SPRITE_DEMO_MAIN_LUA_SRC} ${IR_SPRITE_DEMO_MAIN_LUA_DST}
    COMMAND $<TARGET_FILE:IRSpriteDemo>
    DEPENDS IRSpriteDemo
    WORKING_DIRECTORY ${IR_SPRITE_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
