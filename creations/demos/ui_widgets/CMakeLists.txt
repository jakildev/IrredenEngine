set(IR_UI_WIDGETS_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_UI_WIDGETS_RUNTIME_DIR})
add_executable(IRUIWidgetsDemo main.cpp)
target_link_libraries(IRUIWidgetsDemo PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRUIWidgetsDemo SCRIPTS config.lua)

add_custom_target(IRUIWidgetsDemoRun
    COMMAND $<TARGET_FILE:IRUIWidgetsDemo>
    DEPENDS IRUIWidgetsDemo IRUIWidgetsDemoAssets
    WORKING_DIRECTORY ${IR_UI_WIDGETS_RUNTIME_DIR}
    USES_TERMINAL
)
