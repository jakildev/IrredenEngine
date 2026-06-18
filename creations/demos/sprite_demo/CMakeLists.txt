set(IR_SPRITE_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_SPRITE_DEMO_RUNTIME_DIR})
add_executable(IRSpriteDemo
    main.cpp
    lua_bindings.cpp
)
target_link_libraries(IRSpriteDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRSpriteDemo
    SCRIPTS config.lua scripts/main.lua
    EXTRA_DIRS ${PROJECT_SOURCE_DIR}/assets/demos/sprite_demo assets/sprite_demo
)

add_custom_target(IRSpriteDemoRun
    COMMAND $<TARGET_FILE:IRSpriteDemo>
    DEPENDS IRSpriteDemo IRSpriteDemoAssets
    WORKING_DIRECTORY ${IR_SPRITE_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
