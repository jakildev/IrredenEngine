set(RENDER_CORE_SRC
    src/ir_render.cpp
    src/buffer.cpp
    src/framebuffer.cpp
    src/render_manager.cpp
    src/rendering_rm.cpp
    src/shader.cpp
    src/texture.cpp
    src/vao.cpp
    src/image_data.cpp
)

set(OPENGL_SRC
    src/ir_gl_api.cpp
    src/gl_wrap/GLAPITrace.cpp
    src/opengl/glad.c
    src/opengl/opengl_render_impl.cpp
    src/opengl/opengl_buffer.cpp
    src/opengl/opengl_texture.cpp
    src/opengl/opengl_shader.cpp
    src/opengl/opengl_framebuffer.cpp
    src/opengl/opengl_vertex_layout.cpp
)

set(METAL_SRC
    src/metal/metal_cpp_impl.cpp
    src/metal/metal_render_impl.cpp
    src/metal/metal_runtime.cpp
    src/metal/metal_buffer.cpp
    src/metal/metal_texture.cpp
    src/metal/metal_pipeline.cpp
    src/metal/metal_framebuffer.cpp
    src/metal/metal_cocoa_bridge.mm
)

if(IR_isDarwin)
    # Find Metal and Foundation frameworks
    find_library(METAL_LIB Metal REQUIRED)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    find_library(QUARTZ_LIB QuartzCore REQUIRED)
    find_library(APPKIT_LIB AppKit REQUIRED)

    set(PLATFORM_LIBS ${METAL_LIB} ${FOUNDATION_LIB} ${QUARTZ_LIB} ${APPKIT_LIB})
    set(PLATFORM_SRC ${METAL_SRC})
endif()

if(IR_isWindows OR IR_isLinux)
    set(PLATFORM_SRC ${OPENGL_SRC})
endif()

if(IRREDEN_GRAPHICS_BACKEND STREQUAL "OPENGL")
    set(PLATFORM_SRC ${OPENGL_SRC})
elseif(IRREDEN_GRAPHICS_BACKEND STREQUAL "METAL")
    set(PLATFORM_SRC ${METAL_SRC})
endif()

# OpenGL is linked through system OpenGL on Linux (mesa) and through the
# GLFW-provided gl proc loader on Windows/MinGW. On Linux we also need the
# dl + pthread libs that glad/opengl loaders rely on.
if(IR_isLinux)
    find_package(OpenGL REQUIRED)
    list(APPEND PLATFORM_LIBS OpenGL::GL ${CMAKE_DL_LIBS})
endif()

add_library(IrredenEngineRendering STATIC
    ${RENDER_CORE_SRC}
    ${PLATFORM_SRC}
)
IrredenEngine_setGraphicsBackendCompileDefinitions(IrredenEngineRendering)

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
    IrredenEngineProfile
    IrredenEngineUtility
    IrredenEngineMath
    IrredenEngineInput
    IrredenEngineEntity
    IrredenEngineSystem
    IrredenEngineAsset
    IrredenEngineWindow
    # assimp
    meshoptimizer
    # stbimage
    ${PLATFORM_LIBS}
)
target_include_directories(
    IrredenEngineRendering PUBLIC
    third_party/metal-cpp
)

# add_custom_command(
#     TARGET IrredenEngineRendering
#     POST_BUILD
#     COMMAND ${CMAKE_COMMAND} -E copy
#     ${assimp_BINARY_DIR}/bin/libassimp-5d.dlBl
#     ${PROJECT_BINARY_DIR}/libassimp-5d.dll
# )

target_include_directories(
    IrredenEngineRendering
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/engine/assets

)

if(IR_isDarwin)
    set(METAL_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders/metal)
    file(GLOB METAL_SHADER_SOURCES ${METAL_SHADER_DIR}/*.metal)
    find_program(IRREDEN_XCRUN_BIN xcrun HINTS /usr/bin /Library/Developer/CommandLineTools/usr/bin)

    unset(IRREDEN_METAL_COMPILER)
    unset(IRREDEN_METALLIB_TOOL)
    set(IRREDEN_METAL_COMPILER_RESULT 1)
    set(IRREDEN_METALLIB_TOOL_RESULT 1)
    if(IRREDEN_XCRUN_BIN)
        execute_process(
            COMMAND ${IRREDEN_XCRUN_BIN} --find metal
            RESULT_VARIABLE IRREDEN_METAL_COMPILER_RESULT
            OUTPUT_VARIABLE IRREDEN_METAL_COMPILER
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        execute_process(
            COMMAND ${IRREDEN_XCRUN_BIN} --find metallib
            RESULT_VARIABLE IRREDEN_METALLIB_TOOL_RESULT
            OUTPUT_VARIABLE IRREDEN_METALLIB_TOOL
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
    endif()

    if(
        IRREDEN_XCRUN_BIN
        AND IRREDEN_METAL_COMPILER_RESULT EQUAL 0
        AND IRREDEN_METALLIB_TOOL_RESULT EQUAL 0
        AND IRREDEN_METAL_COMPILER
        AND IRREDEN_METALLIB_TOOL
    )
        file(MAKE_DIRECTORY ${PROJECT_BINARY_DIR}/shaders)
        unset(METAL_AIR_FILES)
        foreach(SHADER ${METAL_SHADER_SOURCES})
            get_filename_component(SHADER_NAME ${SHADER} NAME_WE)
            set(AIR_FILE ${PROJECT_BINARY_DIR}/shaders/${SHADER_NAME}.air)
            add_custom_command(
                OUTPUT ${AIR_FILE}
                COMMAND ${IRREDEN_XCRUN_BIN} -sdk macosx metal -c ${SHADER} -o ${AIR_FILE}
                DEPENDS ${SHADER}
                COMMENT "Compiling Metal shader ${SHADER_NAME}"
            )
            list(APPEND METAL_AIR_FILES ${AIR_FILE})
        endforeach()

        add_custom_command(
            OUTPUT ${PROJECT_BINARY_DIR}/shaders/default.metallib
            COMMAND ${IRREDEN_XCRUN_BIN} -sdk macosx metallib ${METAL_AIR_FILES} -o ${PROJECT_BINARY_DIR}/shaders/default.metallib
            DEPENDS ${METAL_AIR_FILES}
            COMMENT "Linking Metal shader library"
        )

        add_custom_target(MetalShaders ALL DEPENDS ${PROJECT_BINARY_DIR}/shaders/default.metallib)
        add_dependencies(IrredenEngineRendering MetalShaders)
    else()
        message(
            WARNING
            "Metal shader compiler not found via xcrun; skipping .metallib precompilation on this machine. "
            "Install full Xcode and select it with xcode-select if you need precompiled Metal shaders."
        )
    endif()
endif()

add_custom_command(TARGET IrredenEngineRendering POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/src/shaders ${PROJECT_BINARY_DIR}/shaders)

add_custom_command(TARGET IrredenEngineRendering POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/data ${PROJECT_BINARY_DIR}/data)