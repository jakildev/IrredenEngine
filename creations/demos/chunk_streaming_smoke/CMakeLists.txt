set(IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR})
add_executable(IRChunkStreamingSmoke main.cpp)
target_link_libraries(IRChunkStreamingSmoke PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRChunkStreamingSmoke SCRIPTS config.lua)

add_custom_target(IRChunkStreamingSmokeRun
    COMMAND $<TARGET_FILE:IRChunkStreamingSmoke>
    DEPENDS IRChunkStreamingSmoke IRChunkStreamingSmokeAssets
    WORKING_DIRECTORY ${IR_CHUNK_STREAMING_SMOKE_RUNTIME_DIR}
    USES_TERMINAL
)
