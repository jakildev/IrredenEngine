add_library(IrredenEngineCommand STATIC

    src/ir_command.cpp
    src/command_manager.cpp
)

target_compile_options(
    IrredenEngineCommand
    PUBLIC
    -O3
)

target_include_directories(
    IrredenEngineCommand PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    # ${PROJECT_SOURCE_DIR}
)

target_link_libraries(
    IrredenEngineCommand PUBLIC
    IrredenEngineInput
    IrredenEngineEntity
    IrredenEngineAudio
)

# PRIVATE: ir_command.cpp's fireByName dispatcher needs every prefab command
# header in scope. Those headers reach into the IRRender / IRVideo / IRWindow
# umbrellas (and IRSystem for query-driven commands like RANDOMIZE_VOXELS, #17),
# so the TU compiles against those include sets and the static lib carries
# unresolved externals into them — all PRIVATE so the deps do not leak through
# engine/command's PUBLIC interface (every downstream consumer already links
# these libs via engine/world).
target_link_libraries(
    IrredenEngineCommand PRIVATE
    IrredenEngineRendering
    IrredenEngineVideo
    IrredenEngineWindow
    IrredenEngineSystem
)
