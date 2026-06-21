set(IR_SCENE_RESET_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_SCENE_RESET_RUNTIME_DIR})
add_executable(IRSceneReset main.cpp)
target_link_libraries(IRSceneReset PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs) and the
# one-command bundle target IRSceneResetPackage. See cmake/ir_functions.cmake.
irreden_bundle_assets(IRSceneReset SCRIPTS config.lua)
irreden_package_target(IRSceneReset)

# Build + launch from the exe directory (assets land via IRSceneResetAssets).
add_custom_target(IRSceneResetRun
    COMMAND $<TARGET_FILE:IRSceneReset>
    DEPENDS IRSceneReset IRSceneResetAssets
    WORKING_DIRECTORY ${IR_SCENE_RESET_RUNTIME_DIR}
    USES_TERMINAL
)
