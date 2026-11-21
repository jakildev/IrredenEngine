# Script-mode entry point that rewrites the file lists the quality targets
# consume. The quality-file-lists target (irreden_add_quality_targets in
# ir_quality_tools.cmake) runs it ahead of format, format-check,
# format-changed, header-checks and lint:
#
#   cmake -DPROJECT_ROOT=<engine-root> -DOUTPUT_DIR=<build-dir> \
#         -DFORMAT_ROOT=<format-changed diff root> \
#         -P cmake/write_quality_file_lists.cmake
#
# FORMAT_ROOT is what _irreden_resolve_format_root resolved at configure time:
# the engine root, or the user project the build is attached to.

cmake_minimum_required(VERSION 3.20)

foreach(required_arg IN ITEMS PROJECT_ROOT OUTPUT_DIR FORMAT_ROOT)
    if(NOT DEFINED ${required_arg} OR "${${required_arg}}" STREQUAL "")
        message(FATAL_ERROR "${required_arg} is required.")
    endif()
    string(REPLACE "\"" "" ${required_arg} "${${required_arg}}")
endforeach()

# irreden_collect_quality_files resolves its search roots against
# PROJECT_SOURCE_DIR, which script mode never sets for us.
set(PROJECT_SOURCE_DIR "${PROJECT_ROOT}")
include("${PROJECT_ROOT}/cmake/ir_quality_tools.cmake")

irreden_write_quality_file_lists("${OUTPUT_DIR}" "${FORMAT_ROOT}" style_file_count)
