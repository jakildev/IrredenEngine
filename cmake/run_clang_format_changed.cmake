if(NOT DEFINED CLANG_FORMAT_BIN OR CLANG_FORMAT_BIN STREQUAL "")
    message(FATAL_ERROR "CLANG_FORMAT_BIN is required.")
endif()
string(REPLACE "\"" "" CLANG_FORMAT_BIN "${CLANG_FORMAT_BIN}")

if(NOT DEFINED QUALITY_FILE_LIST OR QUALITY_FILE_LIST STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILE_LIST is required.")
endif()
string(REPLACE "\"" "" QUALITY_FILE_LIST "${QUALITY_FILE_LIST}")

if(NOT DEFINED PROJECT_ROOT OR PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()
string(REPLACE "\"" "" PROJECT_ROOT "${PROJECT_ROOT}")

include("${QUALITY_FILE_LIST}")
if(NOT DEFINED QUALITY_FILES OR QUALITY_FILES STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILES is empty. No files to process.")
endif()

# Build the set of files changed on the current branch — committed
# vs the upstream tracking branch (typically origin/master) plus
# anything dirty in the working tree. Matches the worker's intuition:
# "format the files my PR touches", not the engine-wide reformat
# that the bare `format` target produces.

# Pick a useful base for the committed-diff range:
#   - upstream tracking branch if one is set (the common case for an
#     agent-owned worktree branched off origin/master);
#   - fall back to origin/master.
execute_process(
    COMMAND git -C "${PROJECT_ROOT}" rev-parse --abbrev-ref "@{upstream}"
    OUTPUT_VARIABLE _upstream
    RESULT_VARIABLE _upstream_rc
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(_upstream_rc EQUAL 0 AND NOT _upstream STREQUAL "")
    set(_diff_base "${_upstream}")
else()
    set(_diff_base "origin/master")
endif()

function(_collect_git_diff range out_var)
    # `range` is passed as a CMake unquoted variable below; any
    # semicolons in it expand into separate git arguments. Current
    # call sites pass a single-string git range ("base...HEAD" or
    # "HEAD"), but multi-token ranges would also work.
    execute_process(
        COMMAND git -C "${PROJECT_ROOT}" diff --name-only ${range}
        OUTPUT_VARIABLE _out
        RESULT_VARIABLE _rc
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _rc EQUAL 0 OR _out STREQUAL "")
        set(${out_var} "" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "\n" ";" _list "${_out}")
    set(${out_var} "${_list}" PARENT_SCOPE)
endfunction()

set(_changed_files "")
_collect_git_diff("${_diff_base}...HEAD" _committed)
list(APPEND _changed_files ${_committed})
# `git diff --name-only HEAD` covers both staged and unstaged
# working-tree edits in a single call.
_collect_git_diff("HEAD" _working)
list(APPEND _changed_files ${_working})

if(NOT _changed_files)
    message(STATUS "clang-format (changed): no diff vs ${_diff_base}; nothing to format.")
    return()
endif()

list(REMOVE_DUPLICATES _changed_files)

# Intersect against the project's quality file list so generated,
# vendored, and out-of-tree files stay excluded — same filter the
# bare `format` target applies via irreden_collect_quality_files.
set(_targets "")
foreach(_rel IN LISTS _changed_files)
    if(NOT _rel STREQUAL "")
        set(_abs "${PROJECT_ROOT}/${_rel}")
        if(EXISTS "${_abs}")
            list(FIND QUALITY_FILES "${_abs}" _idx)
            if(NOT _idx EQUAL -1)
                list(APPEND _targets "${_abs}")
            endif()
        endif()
    endif()
endforeach()

if(NOT _targets)
    message(STATUS "clang-format (changed): diff vs ${_diff_base} touches no formattable sources.")
    return()
endif()

set(_failed "")
set(_count 0)
foreach(_file IN LISTS _targets)
    math(EXPR _count "${_count} + 1")
    execute_process(
        COMMAND "${CLANG_FORMAT_BIN}" -i --style=file "${_file}"
        RESULT_VARIABLE _rc
        ERROR_VARIABLE _err
    )
    if(NOT _rc EQUAL 0)
        list(APPEND _failed "${_file}")
        if(NOT _err STREQUAL "")
            message(STATUS "${_err}")
        endif()
    endif()
endforeach()

message(STATUS "clang-format (changed) formatted ${_count} file(s) vs ${_diff_base}.")

if(_failed)
    list(JOIN _failed "\n  - " _failed_joined)
    message(FATAL_ERROR
        "clang-format failed for file(s):\n"
        "  - ${_failed_joined}"
    )
endif()
