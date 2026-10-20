set(IR_DEFAULT_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_DEFAULT_RUNTIME_DIR})
add_executable(IRCreationDefault
    main_lua.cpp
    lua_bindings.cpp
)
# add_executable(IRCreationDefault main.cpp)
target_link_libraries(IRCreationDefault PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRCreationDefault SCRIPTS main.lua config.lua commands.lua)

# Run helper target:
# - Launches the creation executable from the build directory
# This lets script edits be picked up without rebuilding C++.
add_custom_target(IRCreationDefaultRun
    COMMAND $<TARGET_FILE:IRCreationDefault>
    DEPENDS IRCreationDefault IRCreationDefaultAssets
    WORKING_DIRECTORY ${IR_DEFAULT_RUNTIME_DIR}
    USES_TERMINAL
)
