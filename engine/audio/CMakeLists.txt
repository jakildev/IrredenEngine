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
if(IR_isDarwin)
    # On macOS we want CoreAudio only. Upstream rtaudio would otherwise
    # happily probe for brew-installed Pulse/Jack and pull them in.
    set(RTAUDIO_API_CORE ON CACHE BOOL "" FORCE)
    set(RTAUDIO_API_JACK OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_PULSE OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_ALSA OFF CACHE BOOL "" FORCE)
    set(RTAUDIO_API_OSS OFF CACHE BOOL "" FORCE)
endif()
# Linux and Windows: let upstream rtaudio's own Linux/Windows defaults win.
# On Linux that is ALSA required + Pulse/Jack auto-probed via pkg-config, on
# Windows it is DirectSound/WASAPI/MMSS. Whichever backends get compiled in
# is controlled by which dev headers the host actually has installed — see
# scripts/bootstrap_linux.sh for the full Linux set.

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
# Linux/Windows: let upstream rtmidi autodetect which backends to enable
# based on the installed dev headers, same reasoning as RtAudio above.

FetchContent_Declare(
    rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG master
    UPDATE_DISCONNECTED TRUE
)
# Upstream rtmidi now sets RTMIDI_TARGETNAME_UNINSTALL to "rt_uninstall" on
# its own, so the old ir_rtmidi.patch that renamed it for us no longer
# applies and is no longer needed. rtaudio's uninstall target is still
# renamed via ir_rtaudio.patch ("RtAudioUninstall"), so both targets coexist
# in one CMake tree without clashing.
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