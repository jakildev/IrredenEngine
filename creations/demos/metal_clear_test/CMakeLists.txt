set(IR_MCT_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_MCT_RUNTIME_DIR})
add_executable(IRMetalClearTest main.cpp)
target_link_libraries(IRMetalClearTest PUBLIC IrredenEngine)

set(IR_MCT_SCRIPTS_DIR ${IR_MCT_RUNTIME_DIR}/scripts)
set(IR_MCT_CONFIG_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_MCT_CONFIG_DST ${IR_MCT_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_MCT_CONFIG_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_MCT_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${IR_MCT_CONFIG_SRC} ${IR_MCT_CONFIG_DST}
    DEPENDS ${IR_MCT_CONFIG_SRC}
    COMMENT "Syncing metal_clear_test config.lua"
)

add_custom_target(IRMetalClearTestAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_MCT_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_MCT_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_MCT_RUNTIME_DIR}/shaders
    DEPENDS ${IR_MCT_CONFIG_DST}
)

add_dependencies(IRMetalClearTest IRMetalClearTestAssets)

add_custom_target(IRMetalClearTestRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_MCT_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_MCT_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_MCT_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_MCT_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRMetalClearTest>
    DEPENDS IRMetalClearTest
    WORKING_DIRECTORY ${IR_MCT_RUNTIME_DIR}
    USES_TERMINAL
)
