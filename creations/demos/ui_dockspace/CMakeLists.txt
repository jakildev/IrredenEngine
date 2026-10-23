set(IR_UI_DOCKSPACE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_UI_DOCKSPACE_RUNTIME_DIR})
add_executable(IRUIDockspace main.cpp)
target_link_libraries(IRUIDockspace PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRUIDockspace SCRIPTS config.lua)

add_custom_target(IRUIDockspaceRun
    COMMAND $<TARGET_FILE:IRUIDockspace>
    DEPENDS IRUIDockspace IRUIDockspaceAssets
    WORKING_DIRECTORY ${IR_UI_DOCKSPACE_RUNTIME_DIR}
    USES_TERMINAL
)
