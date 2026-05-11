set(IR_UI_WIDGETS_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_UI_WIDGETS_RUNTIME_DIR})
add_executable(IRUIWidgetsDemo main.cpp)
target_link_libraries(IRUIWidgetsDemo PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRUIWidgetsDemo
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRUIWidgetsDemo> $<TARGET_RUNTIME_DLLS:IRUIWidgetsDemo>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_UI_WIDGETS_SCRIPTS_DIR ${IR_UI_WIDGETS_RUNTIME_DIR}/scripts)
set(IR_UI_WIDGETS_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_UI_WIDGETS_CONFIG_LUA_DST ${IR_UI_WIDGETS_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_UI_WIDGETS_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_UI_WIDGETS_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_UI_WIDGETS_CONFIG_LUA_SRC} ${IR_UI_WIDGETS_CONFIG_LUA_DST}
    DEPENDS ${IR_UI_WIDGETS_CONFIG_LUA_SRC}
    COMMENT "Syncing ui_widgets config.lua"
)

add_custom_target(IRUIWidgetsDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_UI_WIDGETS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_UI_WIDGETS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_UI_WIDGETS_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_UI_WIDGETS_CONFIG_LUA_DST}
)

add_dependencies(IRUIWidgetsDemo IRUIWidgetsDemoAssets)

add_custom_target(IRUIWidgetsDemoRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_UI_WIDGETS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_UI_WIDGETS_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_UI_WIDGETS_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_UI_WIDGETS_CONFIG_LUA_SRC} ${IR_UI_WIDGETS_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRUIWidgetsDemo>
    DEPENDS IRUIWidgetsDemo
    WORKING_DIRECTORY ${IR_UI_WIDGETS_RUNTIME_DIR}
    USES_TERMINAL
)
