set(IR_AUDIO_PLAYBACK_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_AUDIO_PLAYBACK_RUNTIME_DIR})
add_executable(IRAudioPlayback main.cpp)
target_link_libraries(IRAudioPlayback PUBLIC IrredenEngine)

# Exe-relative runtime layout (data/ shaders/ scripts/ + Windows DLLs).
irreden_bundle_assets(IRAudioPlayback
    SCRIPTS scripts/audio_demo.lua config.lua
    EXTRA_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/data/audio data/audio
)

add_custom_target(IRAudioPlaybackRun
    COMMAND $<TARGET_FILE:IRAudioPlayback>
    DEPENDS IRAudioPlayback IRAudioPlaybackAssets
    WORKING_DIRECTORY ${IR_AUDIO_PLAYBACK_RUNTIME_DIR}
    USES_TERMINAL
)
