set(IR_ANALYTIC_ORACLE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_ANALYTIC_ORACLE_RUNTIME_DIR})
add_executable(IRAnalyticOracle main.cpp)
target_link_libraries(IRAnalyticOracle PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRAnalyticOracle
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRAnalyticOracle> $<TARGET_RUNTIME_DLLS:IRAnalyticOracle>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_ANALYTIC_ORACLE_SCRIPTS_DIR ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}/scripts)
set(IR_ANALYTIC_ORACLE_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_ANALYTIC_ORACLE_CONFIG_LUA_DST ${IR_ANALYTIC_ORACLE_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_ANALYTIC_ORACLE_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_ANALYTIC_ORACLE_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_ANALYTIC_ORACLE_CONFIG_LUA_SRC} ${IR_ANALYTIC_ORACLE_CONFIG_LUA_DST}
    DEPENDS ${IR_ANALYTIC_ORACLE_CONFIG_LUA_SRC}
    COMMENT "Syncing analytic_oracle config.lua"
)

add_custom_target(IRAnalyticOracleAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_ANALYTIC_ORACLE_CONFIG_LUA_DST}
)

add_dependencies(IRAnalyticOracle IRAnalyticOracleAssets)

add_custom_target(IRAnalyticOracleRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_ANALYTIC_ORACLE_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRAnalyticOracle>
    DEPENDS IRAnalyticOracle
    WORKING_DIRECTORY ${IR_ANALYTIC_ORACLE_RUNTIME_DIR}
    USES_TERMINAL
)
