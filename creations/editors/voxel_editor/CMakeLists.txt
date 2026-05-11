set(IR_VOXEL_EDITOR_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_VOXEL_EDITOR_RUNTIME_DIR})
add_executable(IRVoxelEditor main.cpp)
target_link_libraries(IRVoxelEditor PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRVoxelEditor
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRVoxelEditor> $<TARGET_RUNTIME_DLLS:IRVoxelEditor>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_VOXEL_EDITOR_SCRIPTS_DIR ${IR_VOXEL_EDITOR_RUNTIME_DIR}/scripts)
set(IR_VOXEL_EDITOR_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_VOXEL_EDITOR_CONFIG_LUA_DST ${IR_VOXEL_EDITOR_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_VOXEL_EDITOR_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_VOXEL_EDITOR_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_VOXEL_EDITOR_CONFIG_LUA_SRC} ${IR_VOXEL_EDITOR_CONFIG_LUA_DST}
    DEPENDS ${IR_VOXEL_EDITOR_CONFIG_LUA_SRC}
    COMMENT "Syncing voxel_editor config.lua"
)

add_custom_target(IRVoxelEditorAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_VOXEL_EDITOR_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_VOXEL_EDITOR_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_VOXEL_EDITOR_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_VOXEL_EDITOR_CONFIG_LUA_DST}
)

add_dependencies(IRVoxelEditor IRVoxelEditorAssets)

add_custom_target(IRVoxelEditorRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_VOXEL_EDITOR_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_VOXEL_EDITOR_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_VOXEL_EDITOR_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_VOXEL_EDITOR_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRVoxelEditor>
    DEPENDS IRVoxelEditor
    WORKING_DIRECTORY ${IR_VOXEL_EDITOR_RUNTIME_DIR}
    USES_TERMINAL
)
