set(VIDEO_CORE_SRC
    src/ir_video.cpp
    src/video_manager.cpp
    src/video_recorder.cpp
)

set(VIDEO_OPENGL_SRC
    src/opengl/video_backend.cpp
)

set(VIDEO_METAL_SRC
    src/metal/video_backend.cpp
)

if(IR_isDarwin)
    set(VIDEO_PLATFORM_SRC ${VIDEO_METAL_SRC})
endif()

if(IR_isWindows OR IR_isLinux)
    set(VIDEO_PLATFORM_SRC ${VIDEO_OPENGL_SRC})
endif()

if(IRREDEN_GRAPHICS_BACKEND STREQUAL "OPENGL")
    set(VIDEO_PLATFORM_SRC ${VIDEO_OPENGL_SRC})
elseif(IRREDEN_GRAPHICS_BACKEND STREQUAL "METAL")
    set(VIDEO_PLATFORM_SRC ${VIDEO_METAL_SRC})
endif()

add_library(IrredenEngineVideo STATIC
    ${VIDEO_CORE_SRC}
    ${VIDEO_PLATFORM_SRC}
)
IrredenEngine_setGraphicsBackendCompileDefinitions(IrredenEngineVideo)

target_compile_options(
    IrredenEngineVideo
    PUBLIC
    -O3
)

# set(IR_VIDEO_STATIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/static)
# set(IR_VIDEO_DYNAMIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic)

target_link_libraries(
    IrredenEngineVideo PUBLIC
    IrredenEngineProfile
    IrredenEngineUtility
    IrredenEngineTime
    IrredenEngineRendering
    IrredenEngineAudio
)

target_include_directories(
    IrredenEngineVideo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

option(
    IRREDEN_VIDEO_ENABLE_FFMPEG
    "Enable FFmpeg-based video export support"
    ON
)
set(
    IRREDEN_FFMPEG_ROOT
    ""
    CACHE PATH
    "Optional FFmpeg root that contains include/, lib/, and bin/"
)

set(IRREDEN_FFMPEG_ROOT_HINTS "")
if(IR_isDarwin)
    # Prefer the keg-only paths first: they contain only FFmpeg headers and
    # avoid shadowing other FetchContent deps (e.g. fmt) that live under the
    # generic /opt/homebrew/include alongside Homebrew's own fmt package.
    list(APPEND IRREDEN_FFMPEG_ROOT_HINTS
        /opt/homebrew/opt/ffmpeg
        /opt/homebrew
        /usr/local/opt/ffmpeg
        /usr/local
    )
endif()
if(IR_isLinux)
    list(APPEND IRREDEN_FFMPEG_ROOT_HINTS
        /usr
        /usr/local
    )
endif()

set(IRREDEN_VIDEO_HAS_FFMPEG FALSE)
set(IRREDEN_VIDEO_FFMPEG_LIBS "")

if(IRREDEN_VIDEO_ENABLE_FFMPEG)
    set(_ffmpeg_components avcodec avformat avutil swscale)

    if(NOT IRREDEN_FFMPEG_ROOT)
        foreach(_ffmpeg_root_hint IN LISTS IRREDEN_FFMPEG_ROOT_HINTS)
            if(EXISTS "${_ffmpeg_root_hint}/include/libavcodec/avcodec.h")
                set(IRREDEN_FFMPEG_ROOT "${_ffmpeg_root_hint}")
                break()
            endif()
        endforeach()
        # When the root was auto-detected (not user-supplied), clear any stale
        # cached find_path/find_library results so they re-discover from the
        # new root on this configure run.
        if(IRREDEN_FFMPEG_ROOT)
            unset(IRREDEN_FFMPEG_INCLUDE_DIR CACHE)
            foreach(_lib IN LISTS _ffmpeg_components)
                string(TOUPPER "${_lib}" _lib_upper)
                unset(IRREDEN_FFMPEG_${_lib_upper}_LIBRARY CACHE)
            endforeach()
        endif()
    endif()

    if(IRREDEN_FFMPEG_ROOT)
        find_path(
            IRREDEN_FFMPEG_INCLUDE_DIR
            NAMES libavcodec/avcodec.h
            HINTS
            ${IRREDEN_FFMPEG_ROOT}/include
            ${IRREDEN_FFMPEG_ROOT}
            NO_DEFAULT_PATH
        )

        foreach(_ffmpeg_lib IN LISTS _ffmpeg_components)
            string(TOUPPER "${_ffmpeg_lib}" _ffmpeg_lib_upper)
            find_library(
                IRREDEN_FFMPEG_${_ffmpeg_lib_upper}_LIBRARY
                NAMES ${_ffmpeg_lib}
                HINTS
                ${IRREDEN_FFMPEG_ROOT}/lib
                ${IRREDEN_FFMPEG_ROOT}
                NO_DEFAULT_PATH
            )
            if(IRREDEN_FFMPEG_${_ffmpeg_lib_upper}_LIBRARY)
                list(APPEND IRREDEN_VIDEO_FFMPEG_LIBS ${IRREDEN_FFMPEG_${_ffmpeg_lib_upper}_LIBRARY})
            endif()
        endforeach()

        if(IRREDEN_FFMPEG_INCLUDE_DIR AND IRREDEN_VIDEO_FFMPEG_LIBS)
            list(LENGTH IRREDEN_VIDEO_FFMPEG_LIBS _found_ffmpeg_libs)
            list(LENGTH _ffmpeg_components _required_ffmpeg_libs)
            if(_found_ffmpeg_libs EQUAL _required_ffmpeg_libs)
                set(IRREDEN_VIDEO_HAS_FFMPEG TRUE)
                target_include_directories(
                    IrredenEngineVideo PRIVATE
                    ${IRREDEN_FFMPEG_INCLUDE_DIR}
                )
                target_link_libraries(
                    IrredenEngineVideo PUBLIC
                    ${IRREDEN_VIDEO_FFMPEG_LIBS}
                )
            endif()
        endif()
    endif()

    if(NOT IRREDEN_VIDEO_HAS_FFMPEG)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
            pkg_check_modules(
                IRREDEN_FFMPEG QUIET IMPORTED_TARGET
                libavcodec libavformat libavutil libswscale
            )
            if(IRREDEN_FFMPEG_FOUND)
                set(IRREDEN_VIDEO_HAS_FFMPEG TRUE)
                target_link_libraries(IrredenEngineVideo PUBLIC PkgConfig::IRREDEN_FFMPEG)
            endif()
        endif()
    endif()
endif()

if(IRREDEN_VIDEO_HAS_FFMPEG)
    target_compile_definitions(IrredenEngineVideo PUBLIC IR_VIDEO_HAS_FFMPEG=1)
    if(IRREDEN_FFMPEG_ROOT)
        message(STATUS "IrredenEngineVideo: FFmpeg enabled from ${IRREDEN_FFMPEG_ROOT}.")
    else()
        message(STATUS "IrredenEngineVideo: FFmpeg enabled.")
    endif()

    if(IR_isWindows AND IRREDEN_FFMPEG_ROOT)
        add_custom_command(
            TARGET IrredenEngineVideo
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${IRREDEN_FFMPEG_ROOT}/bin
            ${PROJECT_BINARY_DIR}
        )
    endif()
else()
    target_compile_definitions(IrredenEngineVideo PUBLIC IR_VIDEO_HAS_FFMPEG=0)
    message(
        STATUS
        "IrredenEngineVideo: FFmpeg not found, recorder will run in stub mode. "
        "Install ffmpeg + pkg-config (for example via scripts/bootstrap_macos.sh on macOS) "
        "or set IRREDEN_FFMPEG_ROOT."
    )
endif()