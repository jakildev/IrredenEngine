set(IR_LUA_PIPELINE_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR})
add_executable(IRLuaPipelineDemo main_lua.cpp)
target_link_libraries(IRLuaPipelineDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRLuaPipelineDemo SCRIPTS main.lua config.lua)

add_custom_target(IRLuaPipelineDemoRun
    COMMAND $<TARGET_FILE:IRLuaPipelineDemo>
    DEPENDS IRLuaPipelineDemo IRLuaPipelineDemoAssets
    WORKING_DIRECTORY ${IR_LUA_PIPELINE_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
