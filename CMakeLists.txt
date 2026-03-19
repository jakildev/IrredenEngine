cmake_minimum_required(VERSION 3.28.0)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(BuildCreation VERSION 0.1.0)

option(IRREDEN_BUILD_CREATIONS "Build engine-owned creations" ${PROJECT_IS_TOP_LEVEL})
option(IRREDEN_BUILD_TESTS "Build engine test targets" ${PROJECT_IS_TOP_LEVEL})
option(IRREDEN_BUILD_QUALITY_TARGETS "Build engine quality targets" ${PROJECT_IS_TOP_LEVEL})

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# Build-profile defaults:
# - Only true Release builds compile as release-style engine builds.
# - Apply compiler-appropriate optimization flags (MSVC does not use -O3).
add_compile_definitions($<$<CONFIG:Release>:IR_RELEASE>)
if(MSVC)
    add_compile_options(
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:RelWithDebInfo>:/O2>
    )
else()
    add_compile_options(
        $<$<CONFIG:Release>:-O3>
        $<$<CONFIG:RelWithDebInfo>:-O3>
    )
endif()

include(cmake/ir_functions.cmake)
include(cmake/ir_quality_tools.cmake)

include_directories(${PROJECT_SOURCE_DIR}/engine/prefabs)

add_subdirectory(${PROJECT_SOURCE_DIR}/engine)

if(IRREDEN_BUILD_CREATIONS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/creations)
endif()

if(IRREDEN_BUILD_TESTS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/test)
endif()

if(IRREDEN_BUILD_QUALITY_TARGETS)
    irreden_add_quality_targets()
endif()
