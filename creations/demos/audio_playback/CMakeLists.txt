set(IR_AUDIO_PLAYBACK_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_AUDIO_PLAYBACK_RUNTIME_DIR})
add_executable(IRAudioPlayback main.cpp)
target_link_libraries(IRAudioPlayback PUBLIC IrredenEngine)

if(WIN32)
    add_custom_command(
        TARGET IRAudioPlayback
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:IRAudioPlayback> $<TARGET_RUNTIME_DLLS:IRAudioPlayback>
        COMMAND_EXPAND_LISTS
    )
endif()

set(IR_AUDIO_PLAYBACK_SCRIPTS_DIR ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/scripts)
set(IR_AUDIO_PLAYBACK_DEMO_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/scripts/audio_demo.lua)
set(IR_AUDIO_PLAYBACK_CONFIG_LUA_SRC ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
set(IR_AUDIO_PLAYBACK_DEMO_LUA_DST ${IR_AUDIO_PLAYBACK_SCRIPTS_DIR}/audio_demo.lua)
set(IR_AUDIO_PLAYBACK_CONFIG_LUA_DST ${IR_AUDIO_PLAYBACK_SCRIPTS_DIR}/config.lua)
set(IR_AUDIO_PLAYBACK_AUDIO_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/data/audio)
set(IR_AUDIO_PLAYBACK_AUDIO_DST_DIR ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/data/audio)

add_custom_command(
    OUTPUT ${IR_AUDIO_PLAYBACK_DEMO_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${IR_AUDIO_PLAYBACK_SCRIPTS_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_AUDIO_PLAYBACK_DEMO_LUA_SRC} ${IR_AUDIO_PLAYBACK_DEMO_LUA_DST}
    DEPENDS ${IR_AUDIO_PLAYBACK_DEMO_LUA_SRC}
    COMMENT "Syncing audio_playback audio_demo.lua"
)

add_custom_command(
    OUTPUT ${IR_AUDIO_PLAYBACK_CONFIG_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_AUDIO_PLAYBACK_CONFIG_LUA_SRC} ${IR_AUDIO_PLAYBACK_CONFIG_LUA_DST}
    DEPENDS ${IR_AUDIO_PLAYBACK_CONFIG_LUA_SRC}
    COMMENT "Syncing audio_playback config.lua"
)

add_custom_target(IRAudioPlaybackAssets
    # Engine data + shaders (runtime/data, runtime/shaders).
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/shaders
    # Demo audio assets (runtime/data/audio/*.wav) — after the engine data
    # copy so they aren't clobbered.
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_AUDIO_PLAYBACK_AUDIO_SRC_DIR} ${IR_AUDIO_PLAYBACK_AUDIO_DST_DIR}
    DEPENDS
        ${IR_AUDIO_PLAYBACK_DEMO_LUA_DST}
        ${IR_AUDIO_PLAYBACK_CONFIG_LUA_DST}
)

add_dependencies(IRAudioPlayback IRAudioPlaybackAssets)

add_custom_target(IRAudioPlaybackRun
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/data ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/data ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/data
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${IR_AUDIO_PLAYBACK_AUDIO_SRC_DIR} ${IR_AUDIO_PLAYBACK_AUDIO_DST_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_AUDIO_PLAYBACK_DEMO_LUA_SRC} ${IR_AUDIO_PLAYBACK_DEMO_LUA_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${IR_AUDIO_PLAYBACK_CONFIG_LUA_SRC} ${IR_AUDIO_PLAYBACK_CONFIG_LUA_DST}
    COMMAND $<TARGET_FILE:IRAudioPlayback>
    DEPENDS IRAudioPlayback
    WORKING_DIRECTORY ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}
    USES_TERMINAL
)
