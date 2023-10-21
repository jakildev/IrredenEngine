include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/include)
include(FetchContent)
include(CMakeScripts/ir_functions.cmake)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/audio)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/input)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/profiling)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/scripting)


add_library(IrredenEngine STATIC

    src/commands/command_manager.cpp

    src/ecs/archetype_graph.cpp
    src/ecs/archetype_node.cpp
    src/ecs/entity_manager.cpp
    src/ecs/ir_ecs.cpp

    src/foreign/glad/glad.c
    src/foreign/dtv/encoder.cpp
    src/foreign/dtv/frame_queue.cpp
    src/foreign/dtv/frame.cpp

    src/gl_wrap/GLAPITrace.cpp

    src/math/ir_math.cpp

    src/rendering/assimp_demo.cpp
    src/rendering/buffer.cpp
    src/rendering/framebuffer.cpp
    src/rendering/ir_gl_api.cpp
    src/rendering/renderer.cpp
    src/rendering/rendering_rm.cpp
    src/rendering/shader.cpp
    src/rendering/texture.cpp
    src/rendering/vao.cpp
    src/rendering/image_data.cpp

    src/time/time_manager.cpp

    src/world/ir_world.cpp
)

# ------------------ Dependencies start --------------------

# Toggle release build
# target_compile_definitions(IrredenEngine PUBLIC IRREDEN_RELEASE_BUILD)


target_include_directories(IrredenEngine PUBLIC include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/include
)

target_compile_options(
    IrredenEngine
    PUBLIC
    -O3
)

set(IrredenEngine_STATIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/static)
set(IrredenEngine_DYNAMIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic)

# link static lib files
target_link_libraries(IrredenEngine PUBLIC
    ${IrredenEngine_STATIC_LIB_DIR}/libassimp.dll.a
    ${IrredenEngine_STATIC_LIB_DIR}/libmeshoptimizer.a
    -lole32
    ${IrredenEngine_STATIC_LIB_DIR}/libavdevice.a
    ${IrredenEngine_STATIC_LIB_DIR}/libavfilter.a
    ${IrredenEngine_STATIC_LIB_DIR}/libavformat.a
    ${IrredenEngine_STATIC_LIB_DIR}/libswresample.a
    ${IrredenEngine_STATIC_LIB_DIR}/libswscale.a
    ${IrredenEngine_STATIC_LIB_DIR}/libavcodec.a
    -lstrmiids
    -lmfuuid
    ${IrredenEngine_STATIC_LIB_DIR}/libavutil.a
    -lbz2
    -lsecur32
    -lws2_32
    -liconv
    -llzma
    -lz
    -lm
    -luser32
    -lbcrypt
    # ${IrredenEngine_STATIC_LIB_DIR}/libdirect-to-video.a
)
# below could be helpful if trying to pull in package and build here.
#   target_link_libraries(IrredenEngine PUBLIC easy_profiler)
#   target_compile_definitions(IrredenEngine PUBLIC USING_EASY_PROFILER)
#   target_link_libraries(IrredenEngine PUBLIC ${IrredenEngine_STATIC_LIB_DIR}/easy_profiler.lib)

# I built this manually and link here
target_link_libraries(
    IrredenEngine PUBLIC
    IrredenEngineProfiling
    IrredenEngineAudio
    IrredenEngineInput
)

# # Install the IrredenEngine library
# install(TARGETS IrredenEngine
#     ARCHIVE DESTINATION "C:\\Program Files\\IrredenEngine\\lib"
# )

# # Install the IrredenEngine headers
# install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/
#     DESTINATION "C:\\Program Files\\IrredenEngine\\include"
# )

# one way of copying a single file
# configure_file(
#     ${IrredenEngine_DYNAMIC_LIB_DIR}/glfw3.dll
#     ${PROJECT_BINARY_DIR}/glfw3.dll COPYONLY
# )

# configure_file(
#     ${IrredenEngine_DYNAMIC_LIB_DIR}/easy_profiler.dll
#     ${PROJECT_BINARY_DIR}/easy_profiler.dll COPYONLY
# )

configure_file(
    ${IrredenEngine_DYNAMIC_LIB_DIR}/libassimp-5.dll
    ${PROJECT_BINARY_DIR}/libassimp-5.dll COPYONLY
)

# configure_file(
#     ${IrredenEngine_DYNAMIC_LIB_DIR}/libeasy_profiler.dll
#     ${PROJECT_BINARY_DIR}/libeasy_profiler.dll COPYONLY
# )

# configure_file(
#     ${IrredenEngine_DYNAMIC_LIB_DIR}/librtaudiod.dll
#     ${PROJECT_BINARY_DIR}/librtaudiod.dll COPYONLY
# )

# configure_file(
#     ${IrredenEngine_DYNAMIC_LIB_DIR}/librtmidi.dll
#     ${PROJECT_BINARY_DIR}/librtmidi.dll COPYONLY
# )

add_custom_command(TARGET IrredenEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders ${PROJECT_BINARY_DIR}/shaders)

add_custom_command(TARGET IrredenEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/data ${PROJECT_BINARY_DIR}/data)


# add_custom_command(TARGET IrredenEngine POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy_directory
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/scripts ${PROJECT_BINARY_DIR}/scripts)

# add_custom_command(TARGET IrredenEngine POST_BUILD
#                    COMMAND ${CMAKE_COMMAND} -E copy_directory
#                    ${PROJECT_SOURCE_DIR}/foreign/rendering_cookbook/data ${PROJECT_BINARY_DIR}/data)