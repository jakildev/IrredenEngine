# Script-mode entry point for the header convention checks (#2794).
#
# The `header-checks` and `lint` targets only exist after a full CMake
# *configure*, which pulls the whole FetchContent dependency graph and needs a
# compiler. The checks themselves need neither: irreden_collect_quality_files is
# a pure file(GLOB_RECURSE) and run_header_convention_checks.cmake only reads
# text. This wrapper is what lets CI run them on their own:
#
#   cmake -DPROJECT_ROOT=<repo-root> -P cmake/run_header_checks_standalone.cmake
#
# Deliberately a thin shim. It reuses irreden_collect_quality_files for the file
# set and delegates every actual rule to run_header_convention_checks.cmake and
# run_metal_kernel_registry_check.cmake (#2798), so the CI path and the
# `header-checks` / `lint` targets cannot drift into checking different things —
# the drift #2727 was filed about.

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROJECT_ROOT OR PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()
string(REPLACE "\"" "" PROJECT_ROOT "${PROJECT_ROOT}")
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" ABSOLUTE)

if(NOT EXISTS "${PROJECT_ROOT}/cmake/ir_quality_tools.cmake")
    message(FATAL_ERROR
        "PROJECT_ROOT does not look like the engine repo root: ${PROJECT_ROOT}")
endif()

# irreden_collect_quality_files globs relative to PROJECT_SOURCE_DIR, which
# script mode never sets for us.
set(PROJECT_SOURCE_DIR "${PROJECT_ROOT}")
include("${PROJECT_ROOT}/cmake/ir_quality_tools.cmake")

irreden_collect_quality_files(quality_files)
if(quality_files STREQUAL "")
    message(FATAL_ERROR "No files found under ${PROJECT_ROOT}.")
endif()

# run_header_convention_checks.cmake consumes a file list as an includable
# script, not as a variable — write one to a temp path and hand it over.
set(quality_file_list "${CMAKE_CURRENT_LIST_DIR}/../.header-checks-files.cmake")
if(DEFINED OUTPUT_FILE_LIST AND NOT OUTPUT_FILE_LIST STREQUAL "")
    set(quality_file_list "${OUTPUT_FILE_LIST}")
endif()
file(WRITE "${quality_file_list}" "set(QUALITY_FILES\n")
foreach(file_path IN LISTS quality_files)
    file(APPEND "${quality_file_list}" "    \"${file_path}\"\n")
endforeach()
file(APPEND "${quality_file_list}" ")\n")

set(QUALITY_FILE_LIST "${quality_file_list}")
include("${PROJECT_ROOT}/cmake/run_header_convention_checks.cmake")

file(REMOVE "${quality_file_list}")

include("${PROJECT_ROOT}/cmake/run_metal_kernel_registry_check.cmake")
