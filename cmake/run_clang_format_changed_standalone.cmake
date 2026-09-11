# Script-mode entry point for the changed-lines clang-format check (#3187).
#
#   cmake -DPROJECT_ROOT=<repo-root> -DCLANG_FORMAT_BIN=<clang-format> \
#         [-DFORMAT_DIFF_BASE=<commit>] \
#         -P cmake/run_clang_format_changed_standalone.cmake
#
# run_clang_format_changed.cmake hard-requires QUALITY_FILE_LIST — a
# CONFIGURE-time artifact (${PROJECT_BINARY_DIR}/irreden_quality_files.cmake,
# written by irreden_add_quality_targets). Supplying that list without a
# configure is this wrapper's whole job: it is what lets CI run the executor
# without pulling the FetchContent graph and a compiler. Same shape, and same
# reason, as cmake/run_header_checks_standalone.cmake —
# irreden_collect_quality_files is a pure file(GLOB_RECURSE) and the executor
# only reads git and rewrites text, so neither needs a configure.
#
# ONE DELIBERATE DIFFERENCE from that sibling: this call is BARE — no
# INCLUDE_RENDER_BACKENDS. Copying the header shim verbatim is the natural
# move and is wrong here. Per .claude/rules/cpp-globals.md §Detection/Scope
# the wide list backs the header-convention CORRECTNESS gate, while the style
# tools deliberately take the narrow one — that table lists this call as one of
# the two legitimate bare calls. clang-format is a style tool: widening it would
# start rewriting the generated GL wrapper
# (engine/render/include/irreden/render/gl_wrap/) and the Metal backend, which
# are excluded from formatting on purpose. The list is collected rather than
# hand-rolled in the workflow for the opposite reason — a hand-rolled list
# drifts from irreden_collect_quality_files, and a gate whose file set
# disagrees with the tools' is enforcing something nobody can reproduce.
#
# Fix mode is the only mode the executor has; the caller gates on
# `git diff --exit-code` afterwards.

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

if(NOT DEFINED CLANG_FORMAT_BIN OR CLANG_FORMAT_BIN STREQUAL "")
    find_program(_resolved_clang_format NAMES clang-format
        HINTS ${IRREDEN_CLANG_TOOL_HINTS})
    if(NOT _resolved_clang_format)
        message(FATAL_ERROR
            "clang-format not found on PATH. Pass -DCLANG_FORMAT_BIN=<path> "
            "(CI pins an exact version; see .github/workflows/format-check.yml).")
    endif()
    set(CLANG_FORMAT_BIN "${_resolved_clang_format}")
endif()

irreden_collect_quality_files(quality_files)
if(quality_files STREQUAL "")
    message(FATAL_ERROR "No files found under ${PROJECT_ROOT}.")
endif()
list(LENGTH quality_files _quality_count)
# A green format run looks identical whether it scanned the whole quality
# list or nothing at all, so say how many files were collected — a pasted CI log then carries its own proof of
# having looked at something (same reasoning as fleet-rules-sweep's coverage
# line).
message(STATUS
    "clang-format (changed, standalone): ${_quality_count} file(s) on the "
    "quality list (narrow / style scope).")

# run_clang_format_changed.cmake consumes a file list as an includable script,
# not as a variable — write one to a temp path and hand it over.
set(quality_file_list "${PROJECT_ROOT}/.format-changed-files.cmake")
if(DEFINED OUTPUT_FILE_LIST AND NOT OUTPUT_FILE_LIST STREQUAL "")
    set(quality_file_list "${OUTPUT_FILE_LIST}")
endif()
file(WRITE "${quality_file_list}" "set(QUALITY_FILES\n")
foreach(file_path IN LISTS quality_files)
    file(APPEND "${quality_file_list}" "    \"${file_path}\"\n")
endforeach()
file(APPEND "${quality_file_list}" ")\n")

set(QUALITY_FILE_LIST "${quality_file_list}")
include("${PROJECT_ROOT}/cmake/run_clang_format_changed.cmake")

file(REMOVE "${quality_file_list}")
