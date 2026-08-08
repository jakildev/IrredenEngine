# Executed membership check for metal_pipeline.cpp's
# threadgroupSizeForFunctionName registry (#2798). A compute kernel absent
# from the registry silently falls through to the MTL::Size(1, 1, 1)
# fallback -- no assert, no log, no build error -- so this check enumerates
# every dispatchable compute kernel and fails, naming it, on any kernel the
# registry does not mention by name.
#
# Membership only, not value parity: confirming the registered MTL::Size
# equals the GLSL twin's local_size needs to resolve the wrapper -> body
# include chain, which is a materially bigger checker. See the issue for the
# scope boundary.

if(NOT DEFINED PROJECT_ROOT OR PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()
string(REPLACE "\"" "" PROJECT_ROOT "${PROJECT_ROOT}")

set(kernel_dir "${PROJECT_ROOT}/engine/render/src/shaders/metal")
file(GLOB kernel_files "${kernel_dir}/c_*.metal")

set(kernel_stems "")
foreach(kernel_file IN LISTS kernel_files)
    get_filename_component(stem "${kernel_file}" NAME_WE)
    # *_body.metal files are include-fragments referenced by a wrapper, not
    # standalone dispatchable kernels -- they carry no ShaderType::COMPUTE
    # entry point of their own.
    if(stem MATCHES "_body$")
        continue()
    endif()
    list(APPEND kernel_stems "${stem}")
endforeach()

list(LENGTH kernel_stems kernel_count)
if(kernel_count EQUAL 0)
    message(FATAL_ERROR
        "Metal kernel registry check collected 0 kernel file(s) from "
        "${kernel_dir} -- the glob is mis-scoped. Fix the path rather than "
        "trusting a clean result with nothing scanned."
    )
endif()

set(pipeline_cpp "${PROJECT_ROOT}/engine/render/src/metal/metal_pipeline.cpp")
if(NOT EXISTS "${pipeline_cpp}")
    message(FATAL_ERROR "Metal kernel registry check: ${pipeline_cpp} not found.")
endif()

# Line-scoped scan rather than a whole-file regex capture: CMake's regex
# engine has no non-greedy quantifier, so a `.*` capture between the
# function's opening and closing brace would run past nested `}` lines all
# the way to the last `}` in the file. Track entry/exit by indentation
# instead -- every line inside the function body is indented; the function's
# own closing brace is the literal line "}" with no leading whitespace.
file(STRINGS "${pipeline_cpp}" pipeline_lines)

set(in_registry FALSE)
set(found_registry FALSE)
set(found_terminator FALSE)
set(registered_kernels "")
foreach(line IN LISTS pipeline_lines)
    if(NOT in_registry)
        if(line MATCHES "MTL::Size[ \t]+threadgroupSizeForFunctionName\\(")
            set(in_registry TRUE)
            set(found_registry TRUE)
        endif()
        continue()
    endif()
    if(line STREQUAL "}")
        set(found_terminator TRUE)
        break()
    endif()
    # Strip comments before harvesting names. A commented-out entry never
    # reaches the compiled binary, so the kernel it named silently falls
    # through to the MTL::Size(1, 1, 1) fallback while this check reads the
    # dead line and still sees it "registered" -- a false clean in the exact
    # forward direction this check exists to close. Handle both line (//) and
    # block (/* ... */) comments; the sibling scratch-consumer checker applies
    # the identical strip for the identical reason (#2899).
    string(REGEX REPLACE "/\\*.*\\*/" "" line "${line}")
    string(REGEX REPLACE "//.*" "" line "${line}")
    string(REGEX MATCHALL "\"[A-Za-z0-9_]+\"" quoted_names "${line}")
    foreach(quoted_name IN LISTS quoted_names)
        string(REPLACE "\"" "" bare_name "${quoted_name}")
        list(APPEND registered_kernels "${bare_name}")
    endforeach()
endforeach()

if(NOT found_registry)
    message(FATAL_ERROR
        "Metal kernel registry check could not locate "
        "threadgroupSizeForFunctionName's signature in ${pipeline_cpp} -- it "
        "may have been renamed or reformatted; update this checker's anchor."
    )
endif()

# A run past the real terminator sweeps up every quoted string below the
# function -- including functionUsesImageAtomicScratch's kernel names -- so a
# truly unregistered kernel could read as registered: a silent false-clean.
# Reaching EOF without the bare "}" line means the function moved into an
# indented block or the namespace style changed; fail loudly instead.
if(NOT found_terminator)
    message(FATAL_ERROR
        "Metal kernel registry check found threadgroupSizeForFunctionName's "
        "signature but hit EOF before its closing-brace terminator (a line "
        "that is exactly \"}\") in ${pipeline_cpp} -- the function may have "
        "been indented or moved; update this checker's terminator."
    )
endif()

set(missing_kernels "")
foreach(stem IN LISTS kernel_stems)
    list(FIND registered_kernels "${stem}" stem_index)
    if(stem_index EQUAL -1)
        list(APPEND missing_kernels "${stem}")
    endif()
endforeach()

message(STATUS
    "Metal kernel registry check scanned ${kernel_count} compute kernel(s) "
    "against threadgroupSizeForFunctionName."
)

if(missing_kernels)
    list(JOIN missing_kernels ", " missing_kernels_joined)
    message(FATAL_ERROR
        "The following Metal compute kernel(s) have no entry in "
        "threadgroupSizeForFunctionName (${pipeline_cpp}): "
        "${missing_kernels_joined}. An unregistered kernel silently falls "
        "through to the MTL::Size(1, 1, 1) fallback threadgroup size -- add "
        "an explicit entry naming the kernel's real dispatch tile."
    )
endif()
