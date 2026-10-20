set(IR_LAUNCHER_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LAUNCHER_RUNTIME_DIR})
add_executable(IRLauncher main_lua.cpp)
target_link_libraries(IRLauncher PUBLIC IrredenEngine)
# The tool root: the checkout this launcher builds and runs. Trusted state,
# never a runtime argument.
target_compile_definitions(IRLauncher PRIVATE IR_LAUNCHER_ROOT="${CMAKE_SOURCE_DIR}")

irreden_bundle_assets(IRLauncher SCRIPTS
    main.lua
    config.lua
    scripts/launcher_layout.lua
    scripts/catalog_view.lua
    scripts/details.lua
    scripts/log_panel.lua
    scripts/settings.lua
)
