add_library(IrredenEngineRendering STATIC
    # include/irreden/ir_render.hpp
    # include/irreden/render/buffer.hpp
    # include/irreden/render/framebuffer.hpp
    src/ir_render.cpp

    # src/assimp_demo.cpp
    src/buffer.cpp
    src/framebuffer.cpp
    src/ir_gl_api.cpp
    src/render_manager.cpp
    src/rendering_rm.cpp
    src/shader.cpp
    src/texture.cpp
    src/vao.cpp
    src/image_data.cpp

    src/gl_wrap/GLAPITrace.cpp
)

target_compile_options(
    IrredenEngineRendering
    PUBLIC
    -O3
)

# Assimp
# FetchContent_Declare(
#     assimp
#     GIT_REPOSITORY https://github.com/assimp/assimp
#     GIT_TAG v5.3.1
# )
# FetchContent_GetProperties(assimp)
# if(NOT assimp_POPULATED)
#     FetchContent_Populate(assimp)
#     set(ASSIMP_BUILD_TESTS OFF)
#     set(ASSIMP_INSTALL OFF)
# endif()
# add_subdirectory(
#     ${assimp_SOURCE_DIR}
#     ${assimp_BINARY_DIR}
# )

# MeshOptimizer
FetchContent_Declare(
    meshoptimizer
    GIT_REPOSITORY https://github.com/zeux/meshoptimizer
    GIT_TAG master
)
FetchContent_MakeAvailable(
    meshoptimizer
)
target_include_directories(
    IrredenEngineRendering PUBLIC
    ${meshoptimizer_SOURCE_DIR}/src/
)
# StbImage
FetchContent_Declare(
    stbimage
    GIT_REPOSITORY https://github.com/nothings/stb/
    GIT_TAG master
)
FetchContent_MakeAvailable(
    stbimage
)
target_include_directories(
    IrredenEngineRendering PUBLIC
    ${stbimage_SOURCE_DIR}
)

target_link_libraries(
    IrredenEngineRendering PUBLIC
    IrredenEngineProfiling
    IrredenEngineMath
    IrredenEngineInput
    IrredenEngineCommon
    IrredenEngineECS
    # assimp
    meshoptimizer
    # stbimage
)
target_include_directories(
    IrredenEngineRendering PUBLIC
    ${assimp_SOURCE_DIR}/include
)

# add_custom_command(
#     TARGET IrredenEngineRendering
#     POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#     ${assimp_BINARY_DIR}/bin/libassimp-5d.dll
#     ${PROJECT_BINARY_DIR}/libassimp-5d.dll
# )

target_include_directories(
    IrredenEngineRendering
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/engine/assets

)

add_custom_command(TARGET IrredenEngineRendering POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders ${PROJECT_BINARY_DIR}/shaders)

add_custom_command(TARGET IrredenEngineRendering POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/data ${PROJECT_BINARY_DIR}/data)