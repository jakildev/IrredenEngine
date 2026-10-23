set(IR_FONT_MAKER_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_FONT_MAKER_RUNTIME_DIR})
add_executable(IRFontMaker main.cpp)
target_link_libraries(IRFontMaker PUBLIC IrredenEngine)
# The editor opens and saves the engine's trixel-font headers in the source tree.
target_compile_definitions(IRFontMaker PRIVATE
    FONT_MAKER_SHIPPED_FONT_DIR="${CMAKE_SOURCE_DIR}/engine/render/include/irreden/render"
)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRFontMaker SCRIPTS config.lua)

add_custom_target(IRFontMakerRun
    COMMAND $<TARGET_FILE:IRFontMaker>
    DEPENDS IRFontMaker IRFontMakerAssets
    WORKING_DIRECTORY ${IR_FONT_MAKER_RUNTIME_DIR}
    USES_TERMINAL
)
