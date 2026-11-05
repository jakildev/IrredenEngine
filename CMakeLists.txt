cmake_minimum_required(VERSION 3.28.0)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
# Must precede project(): CMake reads it while loading each language's rules.
set(CMAKE_USER_MAKE_RULES_OVERRIDE ${CMAKE_CURRENT_LIST_DIR}/cmake/ir_make_rules_override.cmake)
project(BuildCreation VERSION 0.1.0)

# FetchContent dependencies must not register build-tree paths globally.
set(CMAKE_EXPORT_NO_PACKAGE_REGISTRY ON)
set(CMAKE_FIND_USE_PACKAGE_REGISTRY OFF)

option(IRREDEN_BUILD_CREATIONS "Build creations present in this tree (engine-owned creations/ and nested private checkouts)" ${PROJECT_IS_TOP_LEVEL})
option(IRREDEN_BUILD_TESTS "Build engine test targets" ${PROJECT_IS_TOP_LEVEL})
option(IRREDEN_BUILD_QUALITY_TARGETS "Build engine quality targets" ${PROJECT_IS_TOP_LEVEL})
option(IRREDEN_BUILD_TOOLS "Build standalone tools (img_diff, etc.)" ${PROJECT_IS_TOP_LEVEL})
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

# Default FetchContent deps to static on Linux so creation executables are
# self-contained (no LD_LIBRARY_PATH tweaks or POST_BUILD .so copying).
# FORCE is required because subprojects like easy_profiler do their own
# `set(BUILD_SHARED_LIBS ON CACHE BOOL ...)` without FORCE, which otherwise
# wins over a plain non-cache parent set.
# IR_isLinux isn't set yet at this point, so check CMAKE_SYSTEM_NAME directly.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT DEFINED BUILD_SHARED_LIBS)
    set(
        BUILD_SHARED_LIBS OFF
        CACHE BOOL
        "Build all FetchContent dependencies as static libraries on Linux"
        FORCE
    )
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

# Debug-info depth for Debug builds. The engine and its deps link statically
# into every executable, so full -g repeats the engine's DWARF in each one
# (~330 MB per demo, 1.7 GB for IrredenEngineTest on MinGW). LINES (-g1) keeps
# function names and file:line backtraces in gdb but drops locals, arguments
# and types. FULL is the interactive-debugging default; fleet-up exports
# IRREDEN_DEBUG_INFO=LINES so fleet worktrees configure with the small form.
# The environment only seeds a build dir's first configure — change an
# existing one with -DIRREDEN_DEBUG_INFO=<FULL|LINES>.
# Compressed DWARF is no alternative on MinGW: gcc rejects -gz ("not supported
# in this configuration") and neither ld nor objcopy --compress-debug-sections
# accepts PE.
if(DEFINED ENV{IRREDEN_DEBUG_INFO})
    set(_irDebugInfoDefault "$ENV{IRREDEN_DEBUG_INFO}")
else()
    set(_irDebugInfoDefault FULL)
endif()
set(IRREDEN_DEBUG_INFO "${_irDebugInfoDefault}" CACHE STRING "Debug-build debug info: FULL (-g) or LINES (-g1)")
set_property(CACHE IRREDEN_DEBUG_INFO PROPERTY STRINGS FULL LINES)
string(TOUPPER "${IRREDEN_DEBUG_INFO}" _irDebugInfo)
if(_irDebugInfo STREQUAL "LINES")
    if(NOT MSVC)
        # Lands after CMAKE_<LANG>_FLAGS_DEBUG's -g on the command line, so it wins.
        add_compile_options($<$<CONFIG:Debug>:-g1>)
    endif()
elseif(NOT _irDebugInfo STREQUAL "FULL")
    message(FATAL_ERROR "IRREDEN_DEBUG_INFO must be FULL or LINES (got '${IRREDEN_DEBUG_INFO}')")
endif()

include(cmake/ir_functions.cmake)
include(cmake/ir_quality_tools.cmake)

include_directories(${PROJECT_SOURCE_DIR}/engine/prefabs)

add_subdirectory(${PROJECT_SOURCE_DIR}/engine)

# Lua → C++ codegen tool used by creations and tests via irreden_lua_codegen().
# Added after engine/ so the lua + sol2 targets are already declared.
add_subdirectory(${PROJECT_SOURCE_DIR}/cmake/lua_codegen)

if(IRREDEN_BUILD_CREATIONS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/creations)
endif()

# Gated alongside creations/ so a build that injects a creation worktree
# via IRREDEN_USER_PROJECTS (-DIRREDEN_BUILD_CREATIONS=OFF) can't collide
# with the identically-named targets of the nested main checkout.
if(IRREDEN_BUILD_CREATIONS AND EXISTS "${PROJECT_SOURCE_DIR}/creations/game/CMakeLists.txt")
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
    enable_testing()
    add_subdirectory(${PROJECT_SOURCE_DIR}/test)
endif()

if(IRREDEN_BUILD_TOOLS)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tools/img_diff)
    add_subdirectory(${PROJECT_SOURCE_DIR}/tools/jitter_probe)
endif()

if(IRREDEN_BUILD_QUALITY_TARGETS)
    irreden_add_quality_targets()
endif()
