set(IR_LAUNCHER_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LAUNCHER_RUNTIME_DIR})
add_executable(IRLauncher main_lua.cpp)
target_link_libraries(IRLauncher PUBLIC IrredenEngine)
# The tool root: the checkout this launcher builds and runs. Trusted state,
# never a runtime argument.
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(IR_LAUNCHER_PRESET macos-debug)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(IR_LAUNCHER_PRESET windows-debug)
else()
    set(IR_LAUNCHER_PRESET linux-debug)
endif()
target_compile_definitions(IRLauncher PRIVATE
    IR_LAUNCHER_ROOT="${CMAKE_SOURCE_DIR}"
    IR_LAUNCHER_PRESET="${IR_LAUNCHER_PRESET}"
)

irreden_bundle_assets(IRLauncher SCRIPTS
    main.lua
    config.lua
    scripts/launcher_layout.lua
    scripts/catalog_view.lua
    scripts/details.lua
    scripts/log_panel.lua
    scripts/settings.lua
    scripts/assets.lua
    scripts/widget_list.lua
)
