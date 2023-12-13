add_library(IrredenEngineAudio STATIC
    src/ir_audio.cpp
    src/audio_manager.cpp
    src/midi_in.cpp
    src/midi_out.cpp
    src/audio.cpp
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
IrredenEngine_applyPatchOnce(
    rtaudio
    "fix_rtaudio_target_name_clashes"
    ${CMAKE_CURRENT_SOURCE_DIR}/patches/ir_rtaudio.patch
)
FetchContent_MakeAvailable(rtaudio)
target_link_libraries(IrredenEngineAudio PUBLIC rtaudio)
target_include_directories(IrredenEngineAudio PRIVATE
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
IrredenEngine_applyPatchOnce(
    rtmidi
    "fix_rtmidi_target_name_clashes"
    ${CMAKE_CURRENT_SOURCE_DIR}/patches/ir_rtmidi.patch
)
FetchContent_MakeAvailable(rtmidi)
target_link_libraries(
    IrredenEngineAudio PUBLIC
    rtaudio
    rtmidi
    IrredenEngineProfile
    IrredenEngineECS
    IrredenEngineMath
)
target_include_directories(IrredenEngineAudio PRIVATE
    ${rtmidi_SOURCE_DIR}
)

target_include_directories(
    IrredenEngineAudio PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/engine/assets
)
add_custom_command(
    TARGET IrredenEngineAudio
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${rtmidi_BINARY_DIR}/librtmidid.dll
        ${PROJECT_BINARY_DIR}/librtmidid.dll
)