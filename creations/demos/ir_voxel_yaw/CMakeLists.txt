set(IR_VOXEL_YAW_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_VOXEL_YAW_RUNTIME_DIR})
add_executable(IRVoxelYaw main.cpp)
target_link_libraries(IRVoxelYaw PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRVoxelYaw
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRVoxelYaw> $<TARGET_RUNTIME_DLLS:IRVoxelYaw>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_VOXEL_YAW_SCRIPTS_DIR ${IR_VOXEL_YAW_RUNTIME_DIR}/scripts)
set(IR_VOXEL_YAW_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_VOXEL_YAW_CONFIG_LUA_DST ${IR_VOXEL_YAW_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_VOXEL_YAW_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_VOXEL_YAW_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_VOXEL_YAW_CONFIG_LUA_SRC} ${IR_VOXEL_YAW_CONFIG_LUA_DST}
    DEPENDS ${IR_VOXEL_YAW_CONFIG_LUA_SRC}
    COMMENT "Syncing ir_voxel_yaw config.lua"
)

add_custom_target(IRVoxelYawAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_VOXEL_YAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_VOXEL_YAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_VOXEL_YAW_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_VOXEL_YAW_CONFIG_LUA_DST}
)

add_dependencies(IRVoxelYaw IRVoxelYawAssets)

add_custom_target(IRVoxelYawRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_VOXEL_YAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_VOXEL_YAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_VOXEL_YAW_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_VOXEL_YAW_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRVoxelYaw>
    DEPENDS IRVoxelYaw
    WORKING_DIRECTORY ${IR_VOXEL_YAW_RUNTIME_DIR}
    USES_TERMINAL
)
