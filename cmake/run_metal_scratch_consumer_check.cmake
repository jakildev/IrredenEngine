# Executed membership check for metal_pipeline.cpp's
# functionUsesImageAtomicScratch list (#2878). Twin of
# run_metal_kernel_registry_check.cmake (#2798), which scoped this second
# hand-list out.
#
# Why it matters in both directions. bindComputeResources binds the sticky
# per-frame R32I image-atomic scratch ONLY for kernels this list names, and the
# slot it binds doubles as kBufferIndex_RevoxelizeDetachedParams (the Metal 0-30
# buffer table has no free index):
#
#   * a consumer MISSING from the list never gets the scratch bound -- its
#     imageAtomicMin writes land nowhere;
#   * a non-consumer wrongly ON the list gets the scratch bound over whatever
#     it declared at that slot -- #1619, where c_revoxelize_detached's params
#     UBO was clobbered and the fill read distance-clear words as its params.
#
# So this check compares the two sets both ways rather than only looking for
# omissions.
#
# The expected set is mechanical: a kernel consumes the scratch iff its
# resolved source -- its own file plus the transitive closure of its
# `#include "..."` chain, which is how the *_body.metal fragments reach their
# wrappers -- declares an `atomic_int`/`atomic_uint` parameter at the scratch
# slot. c_revoxelize_detached declares a plain `constant RevoxelizeParams&` at
# that same slot, so the atomic qualifier discriminates it structurally and it
# needs no hand-maintained exclusion entry. That is deliberate: an allowlist
# naming a kernel the matcher already excludes would be a third hand-list
# inside the check written to retire the second one, and it would read as
# load-bearing while protecting nothing. The property is pinned by a negative
# control arm in scripts/fleet/tests/test_header_checks_standalone.sh instead.

if(NOT DEFINED PROJECT_ROOT OR PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()
string(REPLACE "\"" "" PROJECT_ROOT "${PROJECT_ROOT}")

# Read the slot out of the C++ constant rather than hardcoding 16 here. A
# checker carrying its own copy of a number the engine owns goes stale silently
# the day the constant moves; parsing it means a rename or a re-slot fails this
# check loudly instead.
set(runtime_hpp "${PROJECT_ROOT}/engine/render/include/irreden/render/metal/metal_runtime.hpp")
if(NOT EXISTS "${runtime_hpp}")
    message(FATAL_ERROR "Metal scratch-consumer check: ${runtime_hpp} not found.")
endif()
file(STRINGS "${runtime_hpp}" runtime_lines
     REGEX "kMetalImageAtomicScratchSlot[ \t]*=")
set(scratch_slot "")
foreach(line IN LISTS runtime_lines)
    if(line MATCHES "kMetalImageAtomicScratchSlot[ \t]*=[ \t]*([0-9]+)")
        set(scratch_slot "${CMAKE_MATCH_1}")
        break()
    endif()
endforeach()
if(scratch_slot STREQUAL "")
    message(FATAL_ERROR
        "Metal scratch-consumer check could not read "
        "kMetalImageAtomicScratchSlot's value from ${runtime_hpp} -- it may "
        "have been renamed or moved; update this checker's anchor rather than "
        "letting the scan run against a stale slot number."
    )
endif()

set(kernel_dir "${PROJECT_ROOT}/engine/render/src/shaders/metal")
file(GLOB kernel_files "${kernel_dir}/c_*.metal")

set(kernel_stems "")
foreach(kernel_file IN LISTS kernel_files)
    get_filename_component(stem "${kernel_file}" NAME_WE)
    # *_body.metal files are include-fragments pulled in by a wrapper, not
    # standalone dispatchable kernels. They are where two of the five real
    # scratch declarations live, so they are excluded as *entry points* while
    # still being scanned through their wrappers' include closure below.
    if(stem MATCHES "_body$")
        continue()
    endif()
    list(APPEND kernel_stems "${stem}")
endforeach()

list(LENGTH kernel_stems kernel_count)
if(kernel_count EQUAL 0)
    message(FATAL_ERROR
        "Metal scratch-consumer check collected 0 kernel file(s) from "
        "${kernel_dir} -- the glob is mis-scoped. Fix the path rather than "
        "trusting a clean result with nothing scanned."
    )
endif()

# Transitive `#include "..."` closure, resolved against the directory of the
# file doing the including -- the same rule metal_pipeline.cpp's
# loadAndPreprocessMetalSource applies at runtime, with the same visited-set
# cycle guard. Angle-bracket includes are Metal stdlib headers and are skipped.
function(ir_resolve_metal_include_closure entry_file out_var)
    set(pending "${entry_file}")
    set(visited "")
    while(pending)
        list(POP_FRONT pending current)
        list(FIND visited "${current}" seen_index)
        if(NOT seen_index EQUAL -1)
            continue()
        endif()
        list(APPEND visited "${current}")
        get_filename_component(current_dir "${current}" DIRECTORY)
        file(STRINGS "${current}" include_lines REGEX "^[ \t]*#include[ \t]*\"")
        foreach(line IN LISTS include_lines)
            if(NOT line MATCHES "^[ \t]*#include[ \t]*\"([^\"]+)\"")
                continue()
            endif()
            set(included "${current_dir}/${CMAKE_MATCH_1}")
            if(NOT EXISTS "${included}")
                # Silently dropping an unresolvable include would shrink the
                # scanned source of a real consumer and read as "declares no
                # scratch" -- the false-clean this check exists to prevent.
                message(FATAL_ERROR
                    "Metal scratch-consumer check could not resolve "
                    "#include \"${CMAKE_MATCH_1}\" from ${current} "
                    "(looked for ${included})."
                )
            endif()
            list(APPEND pending "${included}")
        endforeach()
    endwhile()
    set(${out_var} "${visited}" PARENT_SCOPE)
endfunction()

set(slot_attribute_regex "\\[\\[[ \t]*buffer\\([ \t]*${scratch_slot}[ \t]*\\)[ \t]*\\]\\]")

# The qualifier test reads the whole parameter declaration, not the physical
# line the slot attribute happens to sit on. A Metal kernel parameter is
# delimited by its neighbours' commas and by the parameter list's own parens, so
# the maximal run of non-delimiter characters ending at the slot attribute IS
# that parameter's declaration head. Matching the window rather than the line
# closes two opposite misreads a line-scoped test is open to:
#
#   * a hand-wrapped declaration (`device atomic_int*` on one line, the name and
#     attribute on the next) reads as declaring no scratch -- the false clean
#     this check exists to close, reached by a line break alone;
#   * a NEIGHBOURING parameter's atomic_int on a shared line reads as this one's
#     qualifier -- a false positive in the #1619 direction.
set(slot_declaration_regex "[^,;(){}]*${slot_attribute_regex}")

set(expected_consumers "")
set(slot_declaration_sites 0)
foreach(stem IN LISTS kernel_stems)
    ir_resolve_metal_include_closure("${kernel_dir}/${stem}.metal" closure)
    set(declares_scratch FALSE)
    foreach(source_file IN LISTS closure)
        file(READ "${source_file}" source_text)
        # Strip line comments while the newlines are still there, then collapse
        # them. Both halves matter: the qualifier test must read the declaration
        # itself and not a trailing comment that happens to say "atomic" (the
        # declaration-head-vs-whole-line precision note .claude/rules/cpp-globals.md
        # carries for the header-global scan), and stripping after the join would
        # let one comment swallow the rest of the parameter list.
        string(REGEX REPLACE "//[^\n]*" "" source_text "${source_text}")
        string(REGEX REPLACE "[\r\n]" " " source_text "${source_text}")
        string(REGEX MATCHALL "${slot_declaration_regex}" slot_declarations
               "${source_text}")
        foreach(declaration IN LISTS slot_declarations)
            math(EXPR slot_declaration_sites "${slot_declaration_sites} + 1")
            if(declaration MATCHES "atomic_(int|uint)")
                set(declares_scratch TRUE)
            endif()
        endforeach()
    endforeach()
    if(declares_scratch)
        list(APPEND expected_consumers "${stem}")
    endif()
endforeach()

# The attribute spelling is the one thing a reformat could break silently in the
# *shrinking* direction only if the list were also empty. It is not -- so a
# broken matcher surfaces as a wall of "listed but does not declare" failures
# below rather than a clean run. This guard covers the remaining case: a tree
# where nothing at all matches, which means the spelling changed rather than
# that the slot genuinely went unused.
if(slot_declaration_sites EQUAL 0)
    message(FATAL_ERROR
        "Metal scratch-consumer check found no [[buffer(${scratch_slot})]] "
        "declaration anywhere under ${kernel_dir}. The slot is aliased and "
        "always has consumers, so this means the attribute spelling changed -- "
        "update this checker's matcher instead of trusting an empty result."
    )
endif()

set(pipeline_cpp "${PROJECT_ROOT}/engine/render/src/metal/metal_pipeline.cpp")
if(NOT EXISTS "${pipeline_cpp}")
    message(FATAL_ERROR "Metal scratch-consumer check: ${pipeline_cpp} not found.")
endif()

# Line-scoped scan with an indentation-keyed terminator, for the reason spelled
# out in run_metal_kernel_registry_check.cmake: CMake's regex engine has no
# non-greedy quantifier, so a brace-to-brace capture would run to the last "}"
# in the file.
file(STRINGS "${pipeline_cpp}" pipeline_lines)

set(in_list FALSE)
set(found_list FALSE)
set(found_terminator FALSE)
set(listed_consumers "")
foreach(line IN LISTS pipeline_lines)
    if(NOT in_list)
        if(line MATCHES "bool[ \t]+functionUsesImageAtomicScratch\\(")
            set(in_list TRUE)
            set(found_list TRUE)
        endif()
        continue()
    endif()
    if(line STREQUAL "}")
        set(found_terminator TRUE)
        break()
    endif()
    # Comment-strip before harvesting names, for the same reason the kernel-side
    # scan does it. A commented-out entry is absent from the list as far as
    # bindComputeResources is concerned, so reading it as present is a false
    # clean in the forward direction: the consumer stops getting the scratch
    # bound, its imageAtomicMin writes land nowhere, and this check stays green.
    # Handles both line (//) and block (/* ... */) comments; the sibling
    # run_metal_kernel_registry_check.cmake's identical scan applies the same
    # two-form strip (#2899).
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" line "${line}")
    string(REGEX REPLACE "//.*" "" line "${line}")
    string(REGEX MATCHALL "\"[A-Za-z0-9_]+\"" quoted_names "${line}")
    foreach(quoted_name IN LISTS quoted_names)
        string(REPLACE "\"" "" bare_name "${quoted_name}")
        list(APPEND listed_consumers "${bare_name}")
    endforeach()
endforeach()

if(NOT found_list)
    message(FATAL_ERROR
        "Metal scratch-consumer check could not locate "
        "functionUsesImageAtomicScratch's signature in ${pipeline_cpp} -- it "
        "may have been renamed or reformatted; update this checker's anchor."
    )
endif()

if(NOT found_terminator)
    message(FATAL_ERROR
        "Metal scratch-consumer check found functionUsesImageAtomicScratch's "
        "signature but hit EOF before its closing-brace terminator (a line "
        "that is exactly \"}\") in ${pipeline_cpp} -- the function may have "
        "been indented or moved; update this checker's terminator."
    )
endif()

set(missing_consumers "")
foreach(stem IN LISTS expected_consumers)
    list(FIND listed_consumers "${stem}" stem_index)
    if(stem_index EQUAL -1)
        list(APPEND missing_consumers "${stem}")
    endif()
endforeach()

set(stale_consumers "")
foreach(name IN LISTS listed_consumers)
    list(FIND expected_consumers "${name}" name_index)
    if(name_index EQUAL -1)
        list(APPEND stale_consumers "${name}")
    endif()
endforeach()

list(LENGTH expected_consumers expected_count)
message(STATUS
    "Metal scratch-consumer check scanned ${kernel_count} compute kernel(s) "
    "against functionUsesImageAtomicScratch (${expected_count} declare the "
    "image-atomic scratch at buffer slot ${scratch_slot})."
)

set(scratch_failures "")
if(missing_consumers)
    list(JOIN missing_consumers ", " missing_joined)
    list(APPEND scratch_failures
        "  declares the image-atomic scratch at buffer slot ${scratch_slot} "
        "but is absent from functionUsesImageAtomicScratch, so "
        "bindComputeResources never binds the scratch for it and its atomic "
        "writes land nowhere: ${missing_joined}\n"
    )
endif()
if(stale_consumers)
    list(JOIN stale_consumers ", " stale_joined)
    list(APPEND scratch_failures
        "  is named by functionUsesImageAtomicScratch but declares no "
        "atomic parameter at buffer slot ${scratch_slot}, so the sticky "
        "scratch bind overwrites whatever it does declare there (#1619): "
        "${stale_joined}\n"
    )
endif()

if(scratch_failures)
    list(JOIN scratch_failures "" scratch_failures_joined)
    message(FATAL_ERROR
        "Metal image-atomic scratch consumer list is out of sync with the "
        "kernel sources (${pipeline_cpp}). The following kernel(s):\n"
        "${scratch_failures_joined}"
        "The list is the only thing gating the per-kernel scratch bind; fix "
        "the list, not this check, unless the kernel's slot usage genuinely "
        "changed."
    )
endif()
