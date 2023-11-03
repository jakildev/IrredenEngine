include(ExternalProject)

add_library(IrredenEngineVideo STATIC
    src/video_recorder.cpp
)

target_compile_options(
    IrredenEngineVideo
    PUBLIC
    -O3
)

set(IR_VIDEO_STATIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/static)
set(IR_VIDEO_DYNAMIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic)

target_link_libraries(
    IrredenEngineVideo PUBLIC
    # FFmpeg libraries and dependencies
    -lole32
    ${IR_VIDEO_STATIC_LIB_DIR}/libavdevice.dll.a
    ${IR_VIDEO_STATIC_LIB_DIR}/libavfilter.dll.a
    ${IR_VIDEO_STATIC_LIB_DIR}/libavformat.dll.a
    ${IR_VIDEO_STATIC_LIB_DIR}/libswresample.dll.a
    ${IR_VIDEO_STATIC_LIB_DIR}/libswscale.dll.a
    ${IR_VIDEO_STATIC_LIB_DIR}/libavcodec.dll.a
    -lstrmiids
    -lmfuuid
    ${IR_VIDEO_STATIC_LIB_DIR}/libavutil.dll.a
    -lbz2
    -lsecur32
    -lws2_32
    -liconv
    -llzma
    -lz
    -lm
    -luser32
    -lbcrypt
    IrredenEngineProfiling
    IrredenEngineRendering
)

target_include_directories(
    IrredenEngineVideo PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

add_custom_command(
    TARGET IrredenEngineVideo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${IR_VIDEO_DYNAMIC_LIB_DIR}/avcodec-60.dll
        ${PROJECT_BINARY_DIR}/avcodec-60.dll
)
add_custom_command(
    TARGET IrredenEngineVideo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${IR_VIDEO_DYNAMIC_LIB_DIR}/avdevice-60.dll
        ${PROJECT_BINARY_DIR}/avdevice-60.dll
)
add_custom_command(
    TARGET IrredenEngineVideo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${IR_VIDEO_DYNAMIC_LIB_DIR}/avfilter-9.dll
        ${PROJECT_BINARY_DIR}/avfilter-9.dll
)
add_custom_command(
    TARGET IrredenEngineVideo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${IR_VIDEO_DYNAMIC_LIB_DIR}/avformat-60.dll
        ${PROJECT_BINARY_DIR}/avformat-60.dll
)
add_custom_command(
    TARGET IrredenEngineVideo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${IR_VIDEO_DYNAMIC_LIB_DIR}/avutil-58.dll
        ${PROJECT_BINARY_DIR}/avutil-58.dll
)
add_custom_command(
    TARGET IrredenEngineVideo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${IR_VIDEO_DYNAMIC_LIB_DIR}/swresample-4.dll
        ${PROJECT_BINARY_DIR}/swresample-4.dll
)
add_custom_command(
    TARGET IrredenEngineVideo
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${IR_VIDEO_DYNAMIC_LIB_DIR}/swscale-7.dll
        ${PROJECT_BINARY_DIR}/swscale-7.dll
)

## NOTES: FFmpeg attempts at not using precompiled binaries. Might
## revisit in the future so leaving here for now.

# ExternalProject_Add(
#     NASM
#     PREFIX ${CMAKE_BINARY_DIR}/_deps/nasm
#     URL https://www.nasm.us/pub/nasm/releasebuilds/2.15.05/win64/nasm-2.15.05-win64.zip
#     CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
#     CONFIGURE_COMMAND bash -c "sh configure"
#     BUILD_COMMAND make everything
#     INSTALL_COMMAND ""
#     LOG_DOWNLOAD ON
# )

# ExternalProject_Add(
#     FFmpeg
#     PREFIX ${CMAKE_BINARY_DIR}/_deps/ffmpeg
#     GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg.git
#     GIT_TAG master
#     # CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_BINARY_DIR}/_deps/ffmpeg/src/FFmpeg-build
#     # CONFIGURE_COMMAND ./<SOURCE_DIR>/configure --prefix=${CMAKE_BINARY_DIR}/_deps/ffmpeg/src/FFmpeg-build
#     # CONFIGURE_COMMAND cmd /c "<SOURCE_DIR>/configure --prefix=${CMAKE_BINARY_DIR}/_deps/ffmpeg/src/FFmpeg-build"
#     CONFIGURE_COMMAND bash -c "cd  <SOURCE_DIR> && ./configure --prefix=<BINARY_DIR>"
#     LOG_CONFIGURE ON
#     LOG_BUILD ON
#     LOG_INSTALL ON
#     BUILD_COMMAND make
#     INSTALL_COMMAND make install
#     BUILD_ALWAYS 1
#     DEPENDS NASM
# )

# find_package(FFmpeg REQUIRED)

# FetchContent_Declare(
#     ffmpeg
#     GIT_REPOSITORY https://github.com/FFmpeg/FFmpeg
#     GIT_TAG master
# )
# FetchContent_MakeAvailable(ffmpeg)
# execute_process(
#     COMMAND ./configure --prefix=${ffmpeg_BINARY_DIR}
#     WORKING_DIRECTORY ${ffmpeg_SOURCE_DIR}
# )
# execute_process(
#     COMMAND make
#     WORKING_DIRECTORY ${ffmpeg_SOURCE_DIR}
# )
# execute_process(
#     COMMAND make install
#     WORKING_DIRECTORY ${ffmpeg_SOURCE_DIR}
# )
# add_custom_target(
#     BuildFFmpeg
#     COMMAND ${CMAKE_COMMAND} -E chdir ${ffmpeg_SOURCE_DIR} make
#     WORKING_DIRECTORY ${ffmpeg_SOURCE_DIR}
# )