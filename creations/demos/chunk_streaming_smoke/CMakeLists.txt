set(IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR})
add_executable(IRChunkStreamingSmoke main.cpp)
target_link_libraries(IRChunkStreamingSmoke PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRChunkStreamingSmoke
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRChunkStreamingSmoke> $<TARGET_RUNTIME_DLLS:IRChunkStreamingSmoke>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_CHUNK_STREAMING_SMOKE_SCRIPTS_DIR ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}/scripts)
set(IR_CHUNK_STREAMING_SMOKE_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_CHUNK_STREAMING_SMOKE_CONFIG_LUA_DST ${IR_CHUNK_STREAMING_SMOKE_SCRIPTS_DIR}/config.lua)

add_custom_command(
    OUTPUT ${IR_CHUNK_STREAMING_SMOKE_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_CHUNK_STREAMING_SMOKE_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_CHUNK_STREAMING_SMOKE_CONFIG_LUA_SRC} ${IR_CHUNK_STREAMING_SMOKE_CONFIG_LUA_DST}
    DEPENDS ${IR_CHUNK_STREAMING_SMOKE_CONFIG_LUA_SRC}
    COMMENT "Syncing chunk_streaming_smoke config.lua"
)

add_custom_target(IRChunkStreamingSmokeAssets
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}/shaders
    DEPENDS
        ${IR_CHUNK_STREAMING_SMOKE_CONFIG_LUA_DST}
)

add_dependencies(IRChunkStreamingSmoke IRChunkStreamingSmokeAssets)

add_custom_target(IRChunkStreamingSmokeRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/config.lua ${IR_CHUNK_STREAMING_SMOKE_SCRIPTS_DIR}/config.lua
    COMMAND $<TARGET_FILE:IRChunkStreamingSmoke>
    DEPENDS IRChunkStreamingSmoke
    WORKING_DIRECTORY ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}
    USES_TERMINAL
)
