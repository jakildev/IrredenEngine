set(IR_SKELETAL_DEMO_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_SKELETAL_DEMO_RUNTIME_DIR})
add_executable(IRSkeletalDemo main.cpp)
target_link_libraries(IRSkeletalDemo PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRSkeletalDemo
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRSkeletalDemo> $<TARGET_RUNTIME_DLLS:IRSkeletalDemo>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_SKELETAL_DEMO_SCRIPTS_DIR ${IR_SKELETAL_DEMO_RUNTIME_DIR}/scripts)
set(IR_SKELETAL_DEMO_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_SKELETAL_DEMO_CONFIG_LUA_DST ${IR_SKELETAL_DEMO_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_SKELETAL_DEMO_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_SKELETAL_DEMO_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_SKELETAL_DEMO_CONFIG_LUA_SRC} ${IR_SKELETAL_DEMO_CONFIG_LUA_DST}
    DEPENDS ${IR_SKELETAL_DEMO_CONFIG_LUA_SRC}
    COMMENT "Syncing skeletal_demo config.lua"
)

add_custom_target(IRSkeletalDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_SKELETAL_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_SKELETAL_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_SKELETAL_DEMO_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_SKELETAL_DEMO_CONFIG_LUA_DST}
)

add_dependencies(IRSkeletalDemo IRSkeletalDemoAssets)

add_custom_target(IRSkeletalDemoRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_SKELETAL_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_SKELETAL_DEMO_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_SKELETAL_DEMO_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_SKELETAL_DEMO_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRSkeletalDemo>
    DEPENDS IRSkeletalDemo
    WORKING_DIRECTORY ${IR_SKELETAL_DEMO_RUNTIME_DIR}
    USES_TERMINAL
)
