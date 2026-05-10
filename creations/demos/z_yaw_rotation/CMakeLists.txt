set(IR_ZYAW_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_ZYAW_RUNTIME_DIR})

set(IR_ZYAW_DEMO_TARGETS
    IRZYawStatic
    IRZYawInteractive
)

set(IR_ZYAW_DEMO_SOURCES
    main_static.cpp
    main_mouse.cpp
)

list(LENGTH IR_ZYAW_DEMO_TARGETS IR_ZYAW_DEMO_COUNT)
math(EXPR IR_ZYAW_DEMO_LAST "${IR_ZYAW_DEMO_COUNT} - 1")

foreach(IR_ZYAW_DEMO_INDEX RANGE ${IR_ZYAW_DEMO_LAST})
    list(GET IR_ZYAW_DEMO_TARGETS ${IR_ZYAW_DEMO_INDEX} IR_ZYAW_DEMO_TARGET)
    list(GET IR_ZYAW_DEMO_SOURCES ${IR_ZYAW_DEMO_INDEX} IR_ZYAW_DEMO_SOURCE)

    add_executable(${IR_ZYAW_DEMO_TARGET} ${IR_ZYAW_DEMO_SOURCE})
    target_link_libraries(${IR_ZYAW_DEMO_TARGET} PUBLIC IrredenEngine)

    if(WIN32)
        add_custom_command(
            TARGET ${IR_ZYAW_DEMO_TARGET}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t
                $<TARGET_FILE_DIR:${IR_ZYAW_DEMO_TARGET}>
                $<TARGET_RUNTIME_DLLS:${IR_ZYAW_DEMO_TARGET}>
            COMMAND_EXPAND_LISTS
        )
    endif()
endforeach()

set(IR_ZYAW_SCRIPTS_DIR ${IR_ZYAW_RUNTIME_DIR}/scripts)
set(IR_ZYAW_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_ZYAW_CONFIG_LUA_DST ${IR_ZYAW_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_ZYAW_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_ZYAW_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_ZYAW_CONFIG_LUA_SRC} ${IR_ZYAW_CONFIG_LUA_DST}
    DEPENDS ${IR_ZYAW_CONFIG_LUA_SRC}
    COMMENT "Syncing z_yaw_rotation config.lua"
)

add_custom_target(IRZYawDemoAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_ZYAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_ZYAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_ZYAW_RUNTIME_DIR}/shaders
    DEPENDS ${IR_ZYAW_CONFIG_LUA_DST}
)

foreach(IR_ZYAW_DEMO_TARGET IN LISTS IR_ZYAW_DEMO_TARGETS)
    add_dependencies(${IR_ZYAW_DEMO_TARGET} IRZYawDemoAssets)
endforeach()

add_custom_target(IRZYawDemos DEPENDS ${IR_ZYAW_DEMO_TARGETS})

add_custom_target(IRZYawStaticRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_ZYAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_ZYAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_ZYAW_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_ZYAW_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRZYawStatic>
    DEPENDS IRZYawStatic
    WORKING_DIRECTORY ${IR_ZYAW_RUNTIME_DIR}
    USES_TERMINAL
)

add_custom_target(IRZYawInteractiveRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_ZYAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_ZYAW_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_ZYAW_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_ZYAW_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRZYawInteractive>
    DEPENDS IRZYawInteractive
    WORKING_DIRECTORY ${IR_ZYAW_RUNTIME_DIR}
    USES_TERMINAL
)
