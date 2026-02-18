cmake_minimum_required(VERSION 3.28.0)
project(BuildCreation VERSION 0.1.0)

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
add_subdirectory(${PROJECT_SOURCE_DIR}/creations)
add_subdirectory(${PROJECT_SOURCE_DIR}/test)

irreden_add_quality_targets()
