set(IR_FONT_MAKER_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_FONT_MAKER_RUNTIME_DIR})
add_executable(EditorFontMaker main.cpp)
target_link_libraries(EditorFontMaker PUBLIC IrredenEngine)
# The editor opens and saves the engine's trixel-font headers in the source tree.
target_compile_definitions(EditorFontMaker PRIVATE
    FONT_MAKER_SHIPPED_FONT_DIR="${CMAKE_SOURCE_DIR}/engine/render/include/irreden/render"
)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(EditorFontMaker SCRIPTS config.lua)

add_custom_target(EditorFontMakerRun
    COMMAND $<TARGET_FILE:EditorFontMaker>
    DEPENDS EditorFontMaker EditorFontMakerAssets
    WORKING_DIRECTORY ${IR_FONT_MAKER_RUNTIME_DIR}
    USES_TERMINAL
)
