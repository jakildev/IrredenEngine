option(IRREDEN_ENABLE_QUALITY_TOOLS "Enable formatter and linter targets." ON)

set(IRREDEN_CLANG_TOOL_HINTS "")
if(APPLE)
    list(APPEND IRREDEN_CLANG_TOOL_HINTS
        /opt/homebrew/opt/llvm/bin
        /usr/local/opt/llvm/bin
    )
endif()

# Drop candidate paths the engine repo's own .gitignore excludes. The glob
# in irreden_collect_quality_files walks the filesystem, not the repository,
# so a gitignored nested checkout under a search root (e.g. a private
# creation's own repo, or an agent worktree inside one) is on disk and would
# otherwise be swept in — untracked content with no owner able to fix a
# header-convention violation reported against it, and a `format` run would
# rewrite files live in another agent's in-progress worktree.
#
# Batches the whole candidate list through one `git check-ignore --stdin`
# call rather than a per-file execute_process (cheap for the ~2k-entry list
# this function produces; per-file would be ~2k process spawns per configure).
function(_irreden_drop_gitignored_files file_list_var)
    find_program(IRREDEN_GIT_EXECUTABLE NAMES git)
    if(NOT IRREDEN_GIT_EXECUTABLE)
        # No git at configure time — leave the candidate list as-is (the
        # pre-fix behavior). git is required to obtain this source tree in
        # the first place, so an absent git at configure time is not a
        # realistic case on any supported host; there is no reject-chain
        # regex fallback here on purpose — a path-substring match can't
        # distinguish a nested checkout under a search root from the
        # engine's own enclosing worktree path also matching the same
        # substring (an agent worktree's own root is
        # ".../.claude/worktrees/<name>", so a "/.claude/worktrees/" test
        # against the *absolute* path matches every file in the tree, not
        # just nested ones — caught in review before this shipped).
        return()
    endif()

    set(candidates "${${file_list_var}}")
    list(LENGTH candidates candidate_count)
    if(candidate_count EQUAL 0)
        return()
    endif()

    set(stdin_file "${CMAKE_BINARY_DIR}/irreden_quality_files_check_ignore_input.txt")
    string(REPLACE ";" "\n" stdin_contents "${candidates}")
    file(WRITE "${stdin_file}" "${stdin_contents}\n")

    execute_process(
        COMMAND "${IRREDEN_GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" check-ignore --stdin
        INPUT_FILE "${stdin_file}"
        OUTPUT_VARIABLE ignored_out
        RESULT_VARIABLE ignored_rc
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    file(REMOVE "${stdin_file}")

    # check-ignore exits 0 (some input matched) or 1 (none matched) on a
    # normal run; anything higher means it errored (e.g. PROJECT_SOURCE_DIR
    # isn't a git repository) — leave the candidate list untouched rather
    # than trust a failed query's (empty) output.
    if(ignored_rc GREATER 1)
        return()
    endif()
    if(ignored_out STREQUAL "")
        return()
    endif()

    string(REPLACE "\n" ";" ignored_list "${ignored_out}")
    list(REMOVE_ITEM candidates ${ignored_list})
    set(${file_list_var} "${candidates}" PARENT_SCOPE)
endfunction()

# `INCLUDE_RENDER_BACKENDS` keeps the generated / vendored graphics sources that
# the style tools skip. See the reject chain below for which consumer wants what.
function(irreden_collect_quality_files out_var)
    cmake_parse_arguments(arg "INCLUDE_RENDER_BACKENDS" "" "" ${ARGN})

    set(search_roots
        "${PROJECT_SOURCE_DIR}/engine"
        "${PROJECT_SOURCE_DIR}/creations"
        "${PROJECT_SOURCE_DIR}/test"
        "${PROJECT_SOURCE_DIR}/tools"
    )

    # CONFIGURE_DEPENDS re-runs the glob on rebuild, but CMake rejects it
    # outside configure mode. run_header_checks_standalone.cmake calls this same
    # function under `cmake -P` so CI can check headers with no configure, so
    # the flag has to drop out there (#2794).
    set(glob_mode CONFIGURE_DEPENDS)
    if(CMAKE_SCRIPT_MODE_FILE)
        set(glob_mode "")
    endif()

    set(globbed_files "")
    foreach(root IN LISTS search_roots)
        if(EXISTS "${root}")
            file(GLOB_RECURSE root_files ${glob_mode}
                "${root}/*.h"
                "${root}/*.hpp"
                "${root}/*.c"
                "${root}/*.cc"
                "${root}/*.cxx"
                "${root}/*.cpp"
                "${root}/*.inl"
            )
            list(APPEND globbed_files ${root_files})
        endif()
    endforeach()

    set(filtered_files "")
    foreach(file_path IN LISTS globbed_files)
        file(TO_CMAKE_PATH "${file_path}" normalized_path)
        if(normalized_path MATCHES "/(build|_deps|third_party)/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/render/src/opengl/glad\\.c$")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/render/third_party/metal-cpp/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/prefabs/irreden/render/systems/copilot_nonesense\\.cpp$")
            continue()
        endif()
        # The generated GL wrapper and the Metal backend are first-party, but
        # the STYLE tools skip them: clang-format rewrites generated code and
        # clang-tidy trips on metal-cpp idioms. That exemption is about style,
        # so it must not reach a correctness gate — the header-convention checks
        # pass INCLUDE_RENDER_BACKENDS to keep them in scope (see #2815).
        # metal-cpp above is vendored and stays rejected in both scopes.
        if(NOT arg_INCLUDE_RENDER_BACKENDS)
            if(normalized_path MATCHES "/engine/render/include/irreden/render/gl_wrap/")
                continue()
            endif()
            if(normalized_path MATCHES "/engine/render/src/metal/")
                continue()
            endif()
            if(normalized_path MATCHES "/engine/render/include/irreden/render/metal/")
                continue()
            endif()
        endif()
        list(APPEND filtered_files "${normalized_path}")
    endforeach()

    list(REMOVE_DUPLICATES filtered_files)
    _irreden_drop_gitignored_files(filtered_files)
    set(${out_var} "${filtered_files}" PARENT_SCOPE)
endfunction()

function(irreden_add_quality_targets)
    if(NOT IRREDEN_ENABLE_QUALITY_TOOLS)
        return()
    endif()

    irreden_collect_quality_files(irreden_quality_files)
    if(irreden_quality_files STREQUAL "")
        message(WARNING "No files found for quality targets.")
        return()
    endif()

    set(irreden_quality_file_list "${PROJECT_BINARY_DIR}/irreden_quality_files.cmake")
    file(WRITE "${irreden_quality_file_list}" "set(QUALITY_FILES\n")
    foreach(file_path IN LISTS irreden_quality_files)
        file(APPEND "${irreden_quality_file_list}" "    \"${file_path}\"\n")
    endforeach()
    file(APPEND "${irreden_quality_file_list}" ")\n")

    # Second, wider list for the header-convention checks only (#2815).
    irreden_collect_quality_files(irreden_header_check_files INCLUDE_RENDER_BACKENDS)
    set(irreden_header_check_file_list
        "${PROJECT_BINARY_DIR}/irreden_header_check_files.cmake")
    file(WRITE "${irreden_header_check_file_list}" "set(QUALITY_FILES\n")
    foreach(file_path IN LISTS irreden_header_check_files)
        file(APPEND "${irreden_header_check_file_list}" "    \"${file_path}\"\n")
    endforeach()
    file(APPEND "${irreden_header_check_file_list}" ")\n")

    find_program(IRREDEN_CLANG_FORMAT_BIN NAMES clang-format HINTS ${IRREDEN_CLANG_TOOL_HINTS})
    if(IRREDEN_CLANG_FORMAT_BIN)
        add_custom_target(format
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_FORMAT_BIN="${IRREDEN_CLANG_FORMAT_BIN}"
                -DQUALITY_FILE_LIST="${irreden_quality_file_list}"
                -DFORMAT_MODE=fix
                -P "${PROJECT_SOURCE_DIR}/cmake/run_clang_format.cmake"
            COMMENT "Formatting source files with clang-format"
            VERBATIM
        )

        add_custom_target(format-check
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_FORMAT_BIN="${IRREDEN_CLANG_FORMAT_BIN}"
                -DQUALITY_FILE_LIST="${irreden_quality_file_list}"
                -DFORMAT_MODE=check
                -P "${PROJECT_SOURCE_DIR}/cmake/run_clang_format.cmake"
            COMMENT "Checking source formatting with clang-format"
            VERBATIM
        )

        # Diff-scoped formatter: only rewrites the lines changed on the
        # current branch (committed vs upstream + working tree), so a
        # touched file's pre-existing drift stays out of the diff.
        # Safe to invoke mid-iteration; the bare `format` target is
        # whole-tree and should only run on intentional cleanup PRs.
        add_custom_target(format-changed
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_FORMAT_BIN="${IRREDEN_CLANG_FORMAT_BIN}"
                -DQUALITY_FILE_LIST="${irreden_quality_file_list}"
                -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_clang_format_changed.cmake"
            COMMENT "Formatting branch-changed source files with clang-format"
            VERBATIM
        )
    else()
        message(STATUS "clang-format not found; format targets will print install guidance.")
        add_custom_target(format
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red
                "clang-format not found. Install LLVM/clang tools and ensure clang-format is on PATH."
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )

        add_custom_target(format-check
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red
                "clang-format not found. Install LLVM/clang tools and ensure clang-format is on PATH."
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )

        add_custom_target(format-changed
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red
                "clang-format not found. Install LLVM/clang tools and ensure clang-format is on PATH."
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )
    endif()

    # Standalone so the header conventions stay runnable without clang-tidy
    # installed — they are pure CMake and have no external tool dependency,
    # but until now they only ran as the second command of `lint`, which
    # exists only when find_program below succeeds.
    add_custom_target(header-checks
        COMMAND ${CMAKE_COMMAND}
            -DQUALITY_FILE_LIST="${irreden_header_check_file_list}"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_header_convention_checks.cmake"
        COMMENT "Running header convention checks"
        VERBATIM
    )

    find_program(IRREDEN_CLANG_TIDY_BIN NAMES clang-tidy HINTS ${IRREDEN_CLANG_TOOL_HINTS})
    if(IRREDEN_CLANG_TIDY_BIN)
        add_custom_target(lint
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_TIDY_BIN="${IRREDEN_CLANG_TIDY_BIN}"
                -DBUILD_DIR="${PROJECT_BINARY_DIR}"
                -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
                -DQUALITY_FILE_LIST="${irreden_quality_file_list}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_clang_tidy.cmake"
            COMMAND ${CMAKE_COMMAND}
                -DQUALITY_FILE_LIST="${irreden_header_check_file_list}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_header_convention_checks.cmake"
            COMMENT "Running lint and header convention checks"
            VERBATIM
        )
    else()
        message(STATUS "clang-tidy not found; lint target will print install guidance.")
        add_custom_target(lint
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red
                "clang-tidy not found. Install LLVM/clang tools and ensure clang-tidy is on PATH."
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )
    endif()
endfunction()
