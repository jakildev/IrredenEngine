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
# Neither diff form reports brand-new untracked files, so without this a
# never-`git add`-ed source file silently skips formatting ("no diff").
execute_process(
    COMMAND git -C "${PROJECT_ROOT}" ls-files --others --exclude-standard
    OUTPUT_VARIABLE _untracked_out
    RESULT_VARIABLE _untracked_rc
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(_untracked "")
if(_untracked_rc EQUAL 0 AND NOT _untracked_out STREQUAL "")
    string(REPLACE "\n" ";" _untracked "${_untracked_out}")
    list(APPEND _changed_files ${_untracked})
endif()

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

# Absolute form of the untracked set, so the formatting loop below can
# tell "brand-new file" (format it whole — all of it is new) from
# "existing file with a few edited lines" (format only those lines).
set(_untracked_abs "")
foreach(_rel IN LISTS _untracked)
    if(NOT _rel STREQUAL "")
        list(APPEND _untracked_abs "${PROJECT_ROOT}/${_rel}")
    endif()
endforeach()

if(NOT _targets)
    message(STATUS "clang-format (changed): diff vs ${_diff_base} touches no formattable sources.")
    return()
endif()

# Line ranges must be expressed in WORKING-TREE coordinates, because that
# is the text clang-format rewrites. So the diff that produces them needs
# the working tree as its post-image: `git diff <merge-base>` (two-dot,
# commit vs working tree) covers committed AND uncommitted edits in one
# pass, while still excluding commits the base gained after we branched —
# the reason the file-list pass above uses three-dot `...HEAD`.
execute_process(
    COMMAND git -C "${PROJECT_ROOT}" merge-base "${_diff_base}" HEAD
    OUTPUT_VARIABLE _range_base
    RESULT_VARIABLE _range_base_rc
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT _range_base_rc EQUAL 0 OR _range_base STREQUAL "")
    set(_range_base "")
    message(STATUS
        "clang-format (changed): no merge-base vs ${_diff_base}; "
        "falling back to whole-file formatting.")
endif()

# Changed line ranges for one file, as repeated `--lines=<start>:<end>`
# arguments — clang-format's own scoping mechanism, and how upstream
# git-clang-format restricts its rewrites.
function(_collect_line_ranges file out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(_range_base STREQUAL "")
        return()
    endif()
    execute_process(
        COMMAND git -C "${PROJECT_ROOT}" diff -U0 "${_range_base}" -- "${file}"
        OUTPUT_VARIABLE _diff_out
        RESULT_VARIABLE _diff_rc
        ERROR_QUIET
    )
    if(NOT _diff_rc EQUAL 0 OR _diff_out STREQUAL "")
        return()
    endif()
    # Every content line in a unified diff carries a '+'/'-'/' ' prefix and
    # -U0 emits no context lines, so "@@" at column 0 is unambiguously a
    # hunk header. Matching against the whole blob (rather than splitting
    # it into a CMake list) keeps ';' inside source lines from corrupting
    # the parse.
    string(REGEX MATCHALL "(^|\n)@@ -[0-9]+(,[0-9]+)? \\+[0-9]+(,[0-9]+)? @@"
        _hunks "${_diff_out}")
    set(_ranges "")
    foreach(_hunk IN LISTS _hunks)
        string(REGEX MATCH "\\+([0-9]+)(,([0-9]+))?" _matched "${_hunk}")
        if(NOT _matched)
            continue()
        endif()
        set(_start "${CMAKE_MATCH_1}")
        set(_len "${CMAKE_MATCH_3}")
        if(_len STREQUAL "")
            set(_len 1)
        endif()
        # '+N,0' is a pure deletion — no post-image lines to format, and
        # clang-format rejects a zero-length range.
        if(_len GREATER 0)
            math(EXPR _end "${_start} + ${_len} - 1")
            list(APPEND _ranges "--lines=${_start}:${_end}")
        endif()
    endforeach()
    set(${out_var} "${_ranges}" PARENT_SCOPE)
endfunction()

set(_failed "")
set(_count 0)
set(_skipped 0)
foreach(_file IN LISTS _targets)
    set(_line_args "")
    list(FIND _untracked_abs "${_file}" _untracked_idx)
    if(_untracked_idx EQUAL -1 AND NOT _range_base STREQUAL "")
        _collect_line_ranges("${_file}" _line_args)
        if(NOT _line_args)
            # Tracked, but no post-image line ranges: a mode-only or
            # rename-only change. Nothing to format — and formatting it
            # whole would drag the file's pre-existing drift into the diff.
            math(EXPR _skipped "${_skipped} + 1")
            continue()
        endif()
    endif()
    math(EXPR _count "${_count} + 1")
    execute_process(
        COMMAND "${CLANG_FORMAT_BIN}" -i --style=file ${_line_args} "${_file}"
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

message(STATUS "clang-format (changed) formatted ${_count} file(s) vs ${_diff_base}, "
    "scoped to changed lines (${_skipped} skipped: no changed lines).")

if(_failed)
    list(JOIN _failed "\n  - " _failed_joined)
    message(FATAL_ERROR
        "clang-format failed for file(s):\n"
        "  - ${_failed_joined}"
    )
endif()
