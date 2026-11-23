option(IRREDEN_ENABLE_QUALITY_TOOLS "Enable formatter and linter targets." ON)

set(IRREDEN_CLANG_TOOL_HINTS "")
if(APPLE)
    list(APPEND IRREDEN_CLANG_TOOL_HINTS
        /opt/homebrew/opt/llvm/bin
        /usr/local/opt/llvm/bin
    )
endif()

# Lists the files under `root` matching one of the `*.<ext>` patterns in ARGN,
# as absolute paths.
#
# The listing is the owning repository's — tracked files plus untracked files
# git does not ignore — and not a walk of the filesystem. A walk descends into
# everything .gitignore excludes: a build tree, a Python virtual environment,
# a nested checkout or agent worktree of another repository. That content has
# no owner able to fix a violation reported against it, and a `format` run
# would rewrite it in place. `git -C <root>` resolves the repository from
# `root` itself, so a downstream creation root that is its own repository is
# listed by that repository, not by the engine's.
#
# A recursive glob is the fallback when `root` is not in a git work tree (a
# source archive) or git cannot answer; there is no ignore list to honour
# there.
function(_irreden_list_source_files out_var root)
    set(listed_files "")
    set(listed_by_git FALSE)

    find_program(IRREDEN_GIT_EXECUTABLE NAMES git)
    if(IRREDEN_GIT_EXECUTABLE)
        # core.quotePath=false: a non-ASCII path otherwise comes back
        # C-quoted, a spelling that names no file on disk.
        execute_process(
            COMMAND "${IRREDEN_GIT_EXECUTABLE}" -C "${root}"
                    -c core.quotePath=false
                    ls-files --cached --others --exclude-standard -- ${ARGN}
            OUTPUT_VARIABLE git_out
            RESULT_VARIABLE git_rc
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(git_rc EQUAL 0)
            set(listed_by_git TRUE)
            string(REPLACE "\n" ";" relative_files "${git_out}")
            foreach(relative_file IN LISTS relative_files)
                # The index still lists a tracked file deleted from the work
                # tree, and an untracked nested repository as a bare directory.
                if(EXISTS "${root}/${relative_file}"
                   AND NOT IS_DIRECTORY "${root}/${relative_file}")
                    list(APPEND listed_files "${root}/${relative_file}")
                endif()
            endforeach()
        endif()
    endif()

    if(NOT listed_by_git)
        set(glob_patterns ${ARGN})
        list(TRANSFORM glob_patterns PREPEND "${root}/")
        file(GLOB_RECURSE listed_files ${glob_patterns})
    endif()

    set(${out_var} "${listed_files}" PARENT_SCOPE)
endfunction()

# `INCLUDE_RENDER_BACKENDS` keeps the generated / vendored graphics sources that
# the style tools skip.
#
# `ROOT <dir>` collects from a tree other than the engine source root — the
# format-changed target passes the downstream creation worktree a build was
# configured against. Only the engine root has the engine's named
# subtree layout, so any other root is listed whole and leans on the reject
# chain plus its repository's ignore rules for its exclusions.
function(irreden_collect_quality_files out_var)
    cmake_parse_arguments(arg "INCLUDE_RENDER_BACKENDS" "ROOT" "" ${ARGN})

    set(collect_root "${PROJECT_SOURCE_DIR}")
    if(arg_ROOT)
        set(collect_root "${arg_ROOT}")
    endif()

    if(collect_root STREQUAL "${PROJECT_SOURCE_DIR}")
        set(search_roots
            "${collect_root}/engine"
            "${collect_root}/creations"
            "${collect_root}/test"
            "${collect_root}/tools"
        )
    else()
        # A downstream creation repo carries its own top-level layout — it
        # has no engine/ or creations/ subtree — so the engine's named search
        # roots match nothing there and the collection comes back empty: the
        # same "looked nowhere" green this function's consumers exist to make
        # impossible.
        set(search_roots "${collect_root}")
    endif()

    set(listed_files "")
    foreach(root IN LISTS search_roots)
        if(EXISTS "${root}")
            _irreden_list_source_files(root_files "${root}"
                "*.h" "*.hpp" "*.c" "*.cc" "*.cxx" "*.cpp" "*.inl")
            list(APPEND listed_files ${root_files})
        endif()
    endforeach()

    set(filtered_files "")
    foreach(file_path IN LISTS listed_files)
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
        # The generated GL wrapper and the Metal backend are first-party, but
        # the STYLE tools skip them: clang-format rewrites generated code and
        # clang-tidy trips on metal-cpp idioms. That exemption is about style,
        # so it must not reach a correctness gate — the header-convention checks
        # pass INCLUDE_RENDER_BACKENDS to keep them in scope.
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
    list(SORT filtered_files)
    set(${out_var} "${filtered_files}" PARENT_SCOPE)
endfunction()

# The source tree a diff-scoped format run measures its changes against.
#
# PROJECT_SOURCE_DIR is NOT that tree whenever a user project is attached: a
# downstream creation worktree has no CMake presets of its own, so ir-build
# configures it through the enclosing ENGINE (`-S <engine>`) with the creation
# passed as `-DIRREDEN_USER_PROJECTS=<creation-worktree>`. The project source
# dir is then the engine while the tree being worked on is the creation's, and
# a formatter keyed on the former diffs a repo nobody edited — a clean answer
# about the wrong question, which reads exactly like a clean tree.
#
# The "has a CMakeLists.txt" test mirrors the root CMakeLists' own user-project
# loop, so a project this returns is always one that was really added.
function(_irreden_resolve_format_root out_var)
    set(candidates "")
    foreach(proj IN LISTS IRREDEN_USER_PROJECTS)
        if(IS_ABSOLUTE "${proj}")
            set(proj_dir "${proj}")
        else()
            set(proj_dir "${PROJECT_SOURCE_DIR}/${proj}")
        endif()
        if(EXISTS "${proj_dir}/CMakeLists.txt")
            list(APPEND candidates "${proj_dir}")
        endif()
    endforeach()

    if(NOT candidates)
        set(${out_var} "${PROJECT_SOURCE_DIR}" PARENT_SCOPE)
        return()
    endif()

    list(GET candidates 0 format_root)
    list(LENGTH candidates candidate_count)
    if(candidate_count GREATER 1)
        # One target, one diff basis. Naming the covered project beats
        # silently formatting the first and reporting clean for the rest —
        # that silence is the failure this resolution exists to close.
        list(JOIN candidates ", " candidates_joined)
        message(WARNING
            "format-changed covers only the first user project (${format_root}); "
            "the rest are not diffed: ${candidates_joined}. Configure one build "
            "tree per creation worktree to format them all.")
    endif()
    set(${out_var} "${format_root}" PARENT_SCOPE)
endfunction()

function(irreden_collect_shader_files out_var)
    set(search_roots
        "${PROJECT_SOURCE_DIR}/engine"
        "${PROJECT_SOURCE_DIR}/creations"
        "${PROJECT_SOURCE_DIR}/test"
        "${PROJECT_SOURCE_DIR}/tools"
    )

    set(listed_files "")
    foreach(root IN LISTS search_roots)
        if(EXISTS "${root}")
            _irreden_list_source_files(root_files "${root}" "*.glsl")
            list(APPEND listed_files ${root_files})
        endif()
    endforeach()

    set(filtered_files "")
    foreach(file_path IN LISTS listed_files)
        file(TO_CMAKE_PATH "${file_path}" normalized_path)
        if(normalized_path MATCHES "/(build|_deps|third_party)/")
            continue()
        endif()
        list(APPEND filtered_files "${normalized_path}")
    endforeach()

    list(REMOVE_DUPLICATES filtered_files)
    list(SORT filtered_files)
    set(${out_var} "${filtered_files}" PARENT_SCOPE)
endfunction()

# Writes `list_path` as an includable script defining QUALITY_FILES — the form
# every run_*.cmake executor takes its file list in. ARGN is the file list.
function(_irreden_write_quality_file_list list_path)
    set(list_contents "set(QUALITY_FILES\n")
    foreach(file_path IN LISTS ARGN)
        string(APPEND list_contents "    \"${file_path}\"\n")
    endforeach()
    string(APPEND list_contents ")\n")
    file(WRITE "${list_path}" "${list_contents}")
endfunction()

# Writes the three lists the quality targets consume into `out_dir`, and
# returns the size of the style-tool list in `style_file_count_var`.
#
#   irreden_quality_files.cmake         format, format-check, clang-tidy
#   irreden_header_check_files.cmake    the header-convention checks; wider,
#                                       it keeps the render backends in scope
#   irreden_format_changed_files.cmake  format-changed, and only when
#                                       `format_root` is a user project: the
#                                       diff-scoped formatter follows the tree
#                                       the build was configured against. With
#                                       no user project that tree is the
#                                       engine's and the style-tool list
#                                       serves.
#
# Called at configure time and again by the quality-file-lists target, so the
# two cannot write different lists.
function(irreden_write_quality_file_lists out_dir format_root style_file_count_var)
    irreden_collect_quality_files(style_files)
    _irreden_write_quality_file_list(
        "${out_dir}/irreden_quality_files.cmake" ${style_files})

    irreden_collect_quality_files(header_check_files INCLUDE_RENDER_BACKENDS)
    _irreden_write_quality_file_list(
        "${out_dir}/irreden_header_check_files.cmake" ${header_check_files})

    if(NOT format_root STREQUAL "${PROJECT_SOURCE_DIR}")
        irreden_collect_quality_files(format_changed_files ROOT "${format_root}")
        _irreden_write_quality_file_list(
            "${out_dir}/irreden_format_changed_files.cmake" ${format_changed_files})
    endif()

    list(LENGTH style_files style_file_count)
    set(${style_file_count_var} "${style_file_count}" PARENT_SCOPE)
endfunction()

function(irreden_add_quality_targets)
    if(NOT IRREDEN_ENABLE_QUALITY_TOOLS)
        return()
    endif()

    _irreden_resolve_format_root(irreden_format_root)
    irreden_write_quality_file_lists(
        "${PROJECT_BINARY_DIR}" "${irreden_format_root}" irreden_style_file_count)
    if(irreden_style_file_count EQUAL 0)
        message(WARNING "No files found for quality targets.")
        return()
    endif()

    set(irreden_quality_file_list "${PROJECT_BINARY_DIR}/irreden_quality_files.cmake")
    set(irreden_header_check_file_list
        "${PROJECT_BINARY_DIR}/irreden_header_check_files.cmake")
    set(irreden_format_changed_file_list "${irreden_quality_file_list}")
    if(NOT irreden_format_root STREQUAL "${PROJECT_SOURCE_DIR}")
        message(STATUS
            "format-changed diff root: ${irreden_format_root} (user project)")
        set(irreden_format_changed_file_list
            "${PROJECT_BINARY_DIR}/irreden_format_changed_files.cmake")
    endif()

    # Every quality target depends on this one, so each run reads lists
    # collected at that moment: a source file added since the last configure
    # is already on them. Tying the lists to the configure instead (a
    # CONFIGURE_DEPENDS glob) re-configures the whole build whenever a
    # matching file appears anywhere under a search root, ignored
    # directories included.
    add_custom_target(quality-file-lists
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
            -DOUTPUT_DIR="${PROJECT_BINARY_DIR}"
            -DFORMAT_ROOT="${irreden_format_root}"
            -P "${PROJECT_SOURCE_DIR}/cmake/write_quality_file_lists.cmake"
        COMMENT "Collecting the quality targets' source file lists"
        VERBATIM
    )

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
                -DQUALITY_FILE_LIST="${irreden_format_changed_file_list}"
                -DFORMAT_ROOT="${irreden_format_root}"
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
    # installed — they are pure CMake and have no external tool dependency.
    add_custom_target(header-checks
        COMMAND ${CMAKE_COMMAND}
            -DQUALITY_FILE_LIST="${irreden_header_check_file_list}"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_header_convention_checks.cmake"
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_metal_kernel_registry_check.cmake"
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_metal_scratch_consumer_check.cmake"
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_save_inventory_population_check.cmake"
        COMMAND ${CMAKE_COMMAND}
            -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/cmake/run_glsl_reserved_word_check.cmake"
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
            COMMAND ${CMAKE_COMMAND}
                -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_metal_kernel_registry_check.cmake"
            COMMAND ${CMAKE_COMMAND}
                -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_metal_scratch_consumer_check.cmake"
            COMMAND ${CMAKE_COMMAND}
                -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_save_inventory_population_check.cmake"
            COMMAND ${CMAKE_COMMAND}
                -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_glsl_reserved_word_check.cmake"
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

    foreach(quality_target IN ITEMS format format-check format-changed header-checks lint)
        add_dependencies(${quality_target} quality-file-lists)
    endforeach()
endfunction()
