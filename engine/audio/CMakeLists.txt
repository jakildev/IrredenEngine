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
set(RTAUDIO_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTAUDIO_BUILD_MODULES OFF CACHE BOOL "" FORCE)
# Pin macOS to CoreAudio only — upstream rtaudio would otherwise probe for
# brew-installed Pulse/Jack and pull them in. Linux/Windows use upstream's
# own autodetect defaults.
if(IR_isDarwin)
    set(RTAUDIO_API_CORE ON CACHE BOOL "" FORCE)
    set(RTAUDIO_API_JACK OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_PULSE OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_ALSA OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_OSS OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    rtaudio
    GIT_REPOSITORY https://github.com/thestk/rtaudio.git
    GIT_TAG master
    UPDATE_DISCONNECTED TRUE
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

IR_copyDLL(
    IrredenEngineAudio
    librtaudiod
    ${rtaudio_BINARY_DIR}
)

# RtMidi
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_MODULES OFF CACHE BOOL "" FORCE)
if(IR_isDarwin)
    set(RTMIDI_API_CORE ON CACHE BOOL "" FORCE)
    set(RTMIDI_API_JACK OFF CACHE BOOL "" FORCE)
    set(RTMIDI_API_ALSA OFF CACHE BOOL "" FORCE)
endif()

FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG master
    UPDATE_DISCONNECTED TRUE
)
# rtmidi's uninstall target no longer clashes with rtaudio's: upstream renamed
# RTMIDI_TARGETNAME_UNINSTALL to "rt_uninstall", and ir_rtaudio.patch renames
# rtaudio's to "RtAudioUninstall".
FetchContent_MakeAvailable(rtmidi)
target_link_libraries(
    IrredenEngineAudio PUBLIC
    rtaudio
    rtmidi
    IrredenEngineProfile
    IrredenEngineEntity
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

IR_copyDLL(
    IrredenEngineAudio
    librtmidid
    ${rtmidi_BINARY_DIR}
)