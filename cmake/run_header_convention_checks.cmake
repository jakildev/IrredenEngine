if(NOT DEFINED QUALITY_FILE_LIST OR QUALITY_FILE_LIST STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILE_LIST is required.")
endif()
string(REPLACE "\"" "" QUALITY_FILE_LIST "${QUALITY_FILE_LIST}")

include("${QUALITY_FILE_LIST}")
if(NOT DEFINED QUALITY_FILES OR QUALITY_FILES STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILES is empty. No files to check.")
endif()

# Headers that still carry banned namespace-scope mutable globals. Mirrors the
# `## Live deviations` section of .claude/rules/cpp-globals.md — the rule text
# and this executor are meant to name the same set, so update both together.
# Ratchet only: a file may leave this list, never join it.
set(header_global_baseline
    "creations/demos/lighting/common/lighting_demo_scene.hpp" # 16 globals, #2728
)

set(anonymous_namespace_failures "")
set(feature_detail_namespace_failures "")
set(header_global_failures "")
set(file_count 0)

foreach(file_path IN LISTS QUALITY_FILES)
    file(TO_CMAKE_PATH "${file_path}" normalized_file_path)
    if(normalized_file_path MATCHES "/(build|_deps|third_party)/")
        continue()
    endif()
    if(NOT normalized_file_path MATCHES "\\.(h|hpp|inl)$")
        continue()
    endif()

    math(EXPR file_count "${file_count} + 1")
    file(READ "${normalized_file_path}" file_contents)

    string(REGEX MATCH "(^|[\r\n])[ \t]*namespace[ \t\r\n]*\\{" has_anonymous_namespace "${file_contents}")
    if(has_anonymous_namespace)
        list(APPEND anonymous_namespace_failures "${normalized_file_path}")
    endif()

    string(REGEX MATCH "namespace[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*Detail[ \t\r\n]*\\{" has_feature_detail_namespace "${file_contents}")
    if(has_feature_detail_namespace)
        list(APPEND feature_detail_namespace_failures "${normalized_file_path}")
    endif()

    # Header-global ban (.claude/rules/cpp-globals.md): no new mutable
    # namespace-scope `inline` / `extern` variable in a header. `constexpr` /
    # `const` are program constants and stay allowed, as do the module entry
    # points that declare the sanctioned manager globals.
    set(is_baselined FALSE)
    foreach(baselined IN LISTS header_global_baseline)
        if(normalized_file_path MATCHES "/${baselined}$")
            set(is_baselined TRUE)
            break()
        endif()
    endforeach()
    # engine/<module>/include/irreden/ir_<module>.hpp + engine/include/irreden/ir_engine.hpp
    if(normalized_file_path MATCHES "/engine/([^/]+/)?include/irreden/ir_[^/]+\\.hpp$")
        set(is_baselined TRUE)
    endif()

    if(NOT is_baselined)
        # CMake's regex engine has no lookahead, so the rule's negative
        # assertions are applied as per-line rejects below rather than inline.
        file(STRINGS "${normalized_file_path}" candidate_lines REGEX "^[ \t]*(inline|extern)[ \t]")
        foreach(line IN LISTS candidate_lines)
            # `extern "C" {` — a linkage block, not a variable.
            if(line MATCHES "^[ \t]*extern[ \t]+\"")
                continue()
            endif()
            # Declaration head only — everything before the initializer or
            # terminator. A whole-line scan would read a trailing comment's
            # "const" as a qualifier and pass real globals as clean.
            string(REGEX REPLACE "[=;{].*$" "" decl_head "${line}")
            # `constexpr` always makes the declared object itself a constant.
            if(decl_head MATCHES "(^|[ \t])constexpr[ \t]")
                continue()
            endif()
            # Strip template argument lists before looking for a pointer
            # declarator — a `*` inside `<...>` belongs to a type argument
            # (`std::array<const char *, N>`), not to the declared object.
            # Repeated to unwrap nesting; CMake has no loop-until-stable.
            set(decl_core "${decl_head}")
            foreach(unused_pass RANGE 3)
                string(REGEX REPLACE "<[^<>]*>" "" decl_core "${decl_core}")
            endforeach()
            if(decl_core MATCHES "\\*")
                # A pointer declaration is a program constant only when BOTH
                # ends are const: `const T *const p`. A leading `const` alone
                # freezes the pointee (`const T *p` is a mutable pointer —
                # reseatable state), and a trailing `const` alone freezes the
                # pointer to still-mutable data. Either way it is unowned
                # process state, which is what this ban is about.
                if(decl_core MATCHES "(^|[ \t])const[ \t]" AND
                   decl_core MATCHES "\\*[ \t]*const([ \t]|$)")
                    continue()
                endif()
            elseif(decl_core MATCHES "(^|[ \t])const[ \t]")
                # Non-pointer `const` (incl. `extern const`, `inline static
                # const`) and `const T &` references are program constants.
                continue()
            endif()
            # A `(` before the initializer means a function declaration, which
            # is what the rule's `[^(]*` guard drops.
            if(line MATCHES "^[^;={]*\\(")
                continue()
            endif()
            if(line MATCHES "(^|[ \t])void[ \t]")
                continue()
            endif()
            if(NOT line MATCHES "[;={]")
                continue()
            endif()
            string(STRIP "${line}" stripped_line)
            # Escape the source line's `;` — CMake lists are `;`-delimited, so
            # an unescaped one splits the entry and emits a blank bullet.
            string(REPLACE ";" "\\;" stripped_line "${stripped_line}")
            list(APPEND header_global_failures "${normalized_file_path}: ${stripped_line}")
        endforeach()
    endif()
endforeach()

list(LENGTH header_global_baseline header_global_baseline_count)
message(STATUS
    "Header convention checks scanned ${file_count} header file(s); "
    "${header_global_baseline_count} file(s) baselined for header-global violations."
)

if(anonymous_namespace_failures)
    list(JOIN anonymous_namespace_failures "\n  - " anonymous_namespace_failures_joined)
    message(FATAL_ERROR
        "Anonymous namespaces are not allowed in headers. Use a nested `detail` namespace "
        "or move the implementation to a .cpp file.\n"
        "  - ${anonymous_namespace_failures_joined}"
    )
endif()

if(feature_detail_namespace_failures)
    list(JOIN feature_detail_namespace_failures "\n  - " feature_detail_namespace_failures_joined)
    message(FATAL_ERROR
        "Feature-specific `*Detail` namespaces are not allowed in headers. Prefer the "
        "nested lowercase `detail` convention unless the helper namespace is an intentional "
        "shared submodule.\n"
        "  - ${feature_detail_namespace_failures_joined}"
    )
endif()

if(header_global_failures)
    list(JOIN header_global_failures "\n  - " header_global_failures_joined)
    message(FATAL_ERROR
        "Mutable namespace-scope globals are not allowed in headers. Every process- or "
        "world-scoped mutable object needs an owner, a lifecycle, and an accessor; route "
        "this one to the pattern that fits its state kind (see the table in "
        ".claude/rules/cpp-globals.md). `constexpr` / `const` constants stay allowed.\n"
        "  - ${header_global_failures_joined}"
    )
endif()
