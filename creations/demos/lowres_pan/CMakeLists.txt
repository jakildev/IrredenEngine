set(IR_LOWRES_PAN_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_LOWRES_PAN_RUNTIME_DIR})
add_executable(IRLowresPan main.cpp)
target_link_libraries(IRLowresPan PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRLowresPan
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRLowresPan> $<TARGET_RUNTIME_DLLS:IRLowresPan>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_LOWRES_PAN_SCRIPTS_DIR ${IR_LOWRES_PAN_RUNTIME_DIR}/scripts)
set(IR_LOWRES_PAN_CONFIG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_LOWRES_PAN_CONFIG_DST ${IR_LOWRES_PAN_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_LOWRES_PAN_CONFIG_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_LOWRES_PAN_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IR_LOWRES_PAN_CONFIG_SRC} ${IR_LOWRES_PAN_CONFIG_DST}
    DEPENDS ${IR_LOWRES_PAN_CONFIG_SRC}
    COMMENT "Syncing lowres_pan config.lua"
)

add_custom_target(IRLowresPanAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_LOWRES_PAN_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_LOWRES_PAN_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_LOWRES_PAN_RUNTIME_DIR}/shaders
    DEPENDS ${IR_LOWRES_PAN_CONFIG_DST}
)

add_dependencies(IRLowresPan IRLowresPanAssets)

add_custom_target(IRLowresPanRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_LOWRES_PAN_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_LOWRES_PAN_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_LOWRES_PAN_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_LOWRES_PAN_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRLowresPan>
    DEPENDS IRLowresPan
    WORKING_DIRECTORY ${IR_LOWRES_PAN_RUNTIME_DIR}
    USES_TERMINAL
)
