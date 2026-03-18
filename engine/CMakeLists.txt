include(FetchContent)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/prefabs
)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
)

add_library(IrredenEngine STATIC
    engine.cpp
)

# set(CMAKE_CXX_SCAN_FOR_MODULES ON)
IrredenEngine_setSystemCompileDefinitions(IrredenEngine)
if(NOT DEFINED IRREDEN_GRAPHICS_BACKEND)
    if(IR_isDarwin)
        set(IRREDEN_GRAPHICS_BACKEND "METAL" CACHE STRING "Rendering backend" FORCE)
    elseif(IR_isWindows)
        set(IRREDEN_GRAPHICS_BACKEND "OPENGL" CACHE STRING "Rendering backend" FORCE)
    else()
        set(IRREDEN_GRAPHICS_BACKEND "OPENGL" CACHE STRING "Rendering backend" FORCE)
    endif()
endif()
set_property(CACHE IRREDEN_GRAPHICS_BACKEND PROPERTY STRINGS OPENGL METAL VULKAN)
IrredenEngine_setGraphicsBackendCompileDefinitions(IrredenEngine)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/audio)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/input)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/profile)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/utility)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/asset)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/script)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/render)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/video)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/math)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/entity)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/common)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/system)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/time)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/world)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/window)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/command)

if(IR_isLinux)
    message(STATUS "Linux-specific code will be compiled.")
elseif(IR_isDarwin)
    message(STATUS "macOS-specific code will be compiled.")
elseif(IR_isWindows)
    message(STATUS "Windows-specific code will be compiled.")
endif()

# ------------------ Dependencies start --------------------

# Toggle release build
# target_compile_definitions(IrredenEngine PUBLIC IRREDEN_RELEASE_BUILD)

target_compile_options(
    IrredenEngine
    PUBLIC
    -O3
)

# set(IrredenEngine_STATIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/static)
# set(IrredenEngine_DYNAMIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic)

# below could be helpful if trying to pull in package and build here.
#   target_link_libraries(IrredenEngine PUBLIC easy_profiler)
# target_compile_definitions(IrredenEngine PUBLIC USING_EASY_PROFILER)
#   target_link_libraries(IrredenEngine PUBLIC ${IrredenEngine_STATIC_LIB_DIR}/easy_profiler.lib)

# I built this manually and link here
target_link_libraries(
    IrredenEngine PUBLIC
    IrredenEngineProfile
    IrredenEngineUtility
    IrredenEngineAsset
    IrredenEngineAudio
    IrredenEngineInput
    IrredenEngineScripting
    IrredenEngineRendering
    IrredenEngineVideo
    IrredenEngineMath
    IrredenEngineEntity
    IrredenEngineSystem
    # IrredenEngineComponent
    IrredenEngineTime
    IrredenEngineCommand
    IrredenEngineWorld
    IrredenEngineWindow
)

target_include_directories(
    IrredenEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_custom_command(TARGET IrredenEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/data ${PROJECT_BINARY_DIR}/data)