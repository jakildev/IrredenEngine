add_library(IrredenEngineAudio STATIC
    audio_manager.cpp
    midi_in.cpp
    midi_out.cpp
    audio.cpp
)

target_compile_options(
    IrredenEngineAudio
    PUBLIC
    -O3
)

# RtAudio
FetchContent_Declare(
    rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG master
)
applyPatchOnce(
    rtaudio
    "fix_rt_target_name_clashes"
    ${PROJECT_SOURCE_DIR}/engine/patches/ir_rtaudio.patch
)
FetchContent_MakeAvailable(rtaudio)
target_link_libraries(IrredenEngineAudio PUBLIC rtaudio)
target_include_directories(IrredenEngineAudio PUBLIC
    ${rtaudio_SOURCE_DIR}
)
add_custom_command(
    TARGET IrredenEngineAudio
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${rtaudio_BINARY_DIR}/librtaudiod.dll
        ${PROJECT_BINARY_DIR}/librtaudiod.dll
)

# RtMidi
FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG master
)
applyPatchOnce(
    rtmidi
    "fix_rt_target_name_clashes"
    ${PROJECT_SOURCE_DIR}/engine/patches/ir_rtmidi.patch
)
FetchContent_MakeAvailable(rtmidi)
target_link_libraries(
    IrredenEngineAudio PUBLIC
    rtmidi
    IrredenEngineProfiling
)
target_include_directories(IrredenEngineAudio PUBLIC
    ${rtmidi_SOURCE_DIR}
)
add_custom_command(
    TARGET IrredenEngineAudio
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${rtmidi_BINARY_DIR}/librtmidid.dll
        ${PROJECT_BINARY_DIR}/librtmidid.dll
)