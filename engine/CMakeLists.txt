
# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/include)
include(FetchContent)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/assets
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/audio)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/input)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/profile)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/script)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/render)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/video)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/math)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/ecs)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/time)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/world)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/common)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/command)

add_library(IrredenEngine STATIC
    engine.cpp
)

# ------------------ Dependencies start --------------------

# Toggle release build
# target_compile_definitions(IrredenEngine PUBLIC IRREDEN_RELEASE_BUILD)

# target_include_directories(IrredenEngine PUBLIC include
#     ${CMAKE_CURRENT_SOURCE_DIR}/src/include

# )

target_compile_options(
    IrredenEngine
    PUBLIC
    -O3
)


# set(IrredenEngine_STATIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/static)
# set(IrredenEngine_DYNAMIC_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/dynamic)

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
    IrredenEngineScripting
    IrredenEngineRendering
    IrredenEngineVideo
    IrredenEngineMath
    IrredenEngineECS
    IrredenEngineTime
    IrredenEngineCommand
    IrredenEngineWorld
)

target_include_directories(
    IrredenEngine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)