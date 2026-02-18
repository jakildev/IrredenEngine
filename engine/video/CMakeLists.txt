add_library(IrredenEngineVideo STATIC
    src/ir_video.cpp
    src/video_manager.cpp
    src/video_recorder.cpp
)

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
    IrredenEngineTime
    IrredenEngineRendering
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

set(IRREDEN_VIDEO_HAS_FFMPEG FALSE)
set(IRREDEN_VIDEO_FFMPEG_LIBS "")

if(IRREDEN_VIDEO_ENABLE_FFMPEG)
    set(_ffmpeg_components avcodec avformat avutil swscale)

    if(IRREDEN_FFMPEG_ROOT)
        find_path(
            IRREDEN_FFMPEG_INCLUDE_DIR
            NAMES libavcodec/avcodec.h
            HINTS ${IRREDEN_FFMPEG_ROOT}/include
        )

        foreach(_ffmpeg_lib IN LISTS _ffmpeg_components)
            string(TOUPPER "${_ffmpeg_lib}" _ffmpeg_lib_upper)
            find_library(
                IRREDEN_FFMPEG_${_ffmpeg_lib_upper}_LIBRARY
                NAMES ${_ffmpeg_lib}
                HINTS ${IRREDEN_FFMPEG_ROOT}/lib
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
    message(STATUS "IrredenEngineVideo: FFmpeg enabled.")

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
    message(STATUS "IrredenEngineVideo: FFmpeg not found, recorder will run in stub mode.")
endif()