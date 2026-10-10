set(IR_SHAPE_DEBUG_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_SHAPE_DEBUG_RUNTIME_DIR})
add_executable(IRShapeDebug main.cpp)
target_link_libraries(IRShapeDebug PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs) and the
# one-command bundle target IRShapeDebugPackage. See cmake/ir_functions.cmake.
irreden_bundle_assets(IRShapeDebug
    SCRIPTS config.lua
    EXTRA_DIRS
        ${CMAKE_CURRENT_SOURCE_DIR}/configs configs
        ${PROJECT_SOURCE_DIR}/assets/prefabs assets/prefabs
        ${PROJECT_SOURCE_DIR}/creations/editors/voxel_editor/test/fixtures/editor_parts
        assets/prefabs/editor_parts
)
irreden_package_target(IRShapeDebug)

# Build + launch from the exe directory (assets land via IRShapeDebugAssets).
add_custom_target(IRShapeDebugRun
    COMMAND $<TARGET_FILE:IRShapeDebug>
    DEPENDS IRShapeDebug IRShapeDebugAssets
    WORKING_DIRECTORY ${IR_SHAPE_DEBUG_RUNTIME_DIR}
    USES_TERMINAL
)
