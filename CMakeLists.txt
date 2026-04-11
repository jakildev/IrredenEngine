cmake_minimum_required(VERSION 3.28.0)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
project(BuildCreation VERSION 0.1.0)

option(IRREDEN_BUILD_CREATIONS "Build engine-owned creations" ${PROJECT_IS_TOP_LEVEL})
option(IRREDEN_BUILD_TESTS "Build engine test targets" ${PROJECT_IS_TOP_LEVEL})
option(IRREDEN_BUILD_QUALITY_TARGETS "Build engine quality targets" ${PROJECT_IS_TOP_LEVEL})
set(
    IRREDEN_USER_PROJECTS
    ""
    CACHE STRING
    "Semicolon-separated list of additional CMake projects to add after the engine"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# set(CMAKE_CXX_SCAN_FOR_MODULES ON)

# On Linux, default every fetched third-party dependency to static so the
# resulting creation executables are self-contained and do not require
# LD_LIBRARY_PATH tweaks or POST_BUILD shared-object copying. Windows ships
# MinGW runtime DLLs alongside the exe and macOS has Homebrew on the rpath,
# so the default there is unchanged.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DEFINED BUILD_SHARED_LIBS)
    set(BUILD_SHARED_LIBS OFF)
endif()

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

if(EXISTS "${PROJECT_SOURCE_DIR}/creations/game/CMakeLists.txt")
    add_subdirectory(${PROJECT_SOURCE_DIR}/creations/game)
endif()

if(IRREDEN_USER_PROJECTS)
    foreach(proj IN LISTS IRREDEN_USER_PROJECTS)
        if(IS_ABSOLUTE "${proj}")
            set(proj_dir "${proj}")
        else()
            set(proj_dir "${PROJECT_SOURCE_DIR}/${proj}")
        endif()

        if(EXISTS "${proj_dir}/CMakeLists.txt")
            get_filename_component(proj_name "${proj}" NAME)
            add_subdirectory(
                "${proj_dir}"
                "${CMAKE_CURRENT_BINARY_DIR}/user_projects/${proj_name}"
            )
        else()
            message(
                WARNING
                "IRREDEN_USER_PROJECTS: skipping '${proj}' because no "
                "CMakeLists.txt was found at '${proj_dir}'"
            )
        endif()
    endforeach()
endif()

if(IRREDEN_BUILD_TESTS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/test)
endif()

if(IRREDEN_BUILD_QUALITY_TARGETS)
    irreden_add_quality_targets()
endif()
