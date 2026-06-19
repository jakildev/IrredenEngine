set(IR_UI_DOCKSPACE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_UI_DOCKSPACE_RUNTIME_DIR})
add_executable(IRUiDockspace main.cpp)
target_link_libraries(IRUiDockspace PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRUiDockspace SCRIPTS config.lua)

add_custom_target(IRUiDockspaceRun
    COMMAND $<TARGET_FILE:IRUiDockspace>
    DEPENDS IRUiDockspace IRUiDockspaceAssets
    WORKING_DIRECTORY ${IR_UI_DOCKSPACE_RUNTIME_DIR}
    USES_TERMINAL
)
