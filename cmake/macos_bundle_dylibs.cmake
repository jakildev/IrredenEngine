# macos_bundle_dylibs.cmake — make a staged macOS executable relocatable.
#
# Invoked by irreden_package_target() (cmake/ir_functions.cmake) as:
#   cmake -DEXE=<staged exe> -DDEST=<staging dir> -P macos_bundle_dylibs.cmake
#
# Copies the executable's non-system dynamic libraries next to it and rewrites
# the exe's load commands to @executable_path so the unzipped folder runs
# without the build tree or Homebrew on the load path.
#
# SHALLOW bundle: only the exe's DIRECT dependencies are walked. Transitive
# Homebrew deps (e.g. ffmpeg's codec libraries) are not yet followed, so a
# truly clean-box bundle is follow-up work — on the build host the copied libs
# still resolve their own deps via their absolute install paths. System
# libraries (/usr/lib, /System, framework SDKs) are never bundled.

if(NOT DEFINED EXE OR NOT DEFINED DEST)
    message(FATAL_ERROR "macos_bundle_dylibs: -DEXE and -DDEST are required")
endif()
if(NOT EXISTS "${EXE}")
    message(FATAL_ERROR "macos_bundle_dylibs: staged exe not found: ${EXE}")
endif()

get_filename_component(_exe_dir "${EXE}" DIRECTORY)

# --- Collect the exe's LC_RPATH entries (to resolve @rpath/... references) ----
execute_process(COMMAND otool -l "${EXE}" OUTPUT_VARIABLE _load_cmds
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "macos_bundle_dylibs: otool -l failed on ${EXE}")
endif()
string(REPLACE "\n" ";" _lc_lines "${_load_cmds}")
set(_rpaths "")
set(_expect_path FALSE)
foreach(_line IN LISTS _lc_lines)
    if(_line MATCHES "cmd LC_RPATH")
        set(_expect_path TRUE)
    elseif(_expect_path AND _line MATCHES "path (.+) \\(offset")
        list(APPEND _rpaths "${CMAKE_MATCH_1}")
        set(_expect_path FALSE)
    endif()
endforeach()

# --- Walk the exe's direct dylib references ----------------------------------
execute_process(COMMAND otool -L "${EXE}" OUTPUT_VARIABLE _linked
                RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
    message(FATAL_ERROR "macos_bundle_dylibs: otool -L failed on ${EXE}")
endif()
string(REPLACE "\n" ";" _link_lines "${_linked}")

# Resolve an @rpath/@loader_path/@executable_path/absolute reference to a real
# file path, or empty if it cannot be found.
function(_resolve_ref ref out_var)
    set(${out_var} "" PARENT_SCOPE)
    if(ref MATCHES "^@rpath/(.+)$")
        set(_rest "${CMAKE_MATCH_1}")
        foreach(_rp IN LISTS _rpaths)
            string(REPLACE "@loader_path" "${_exe_dir}" _rp_abs "${_rp}")
            string(REPLACE "@executable_path" "${_exe_dir}" _rp_abs "${_rp_abs}")
            if(EXISTS "${_rp_abs}/${_rest}")
                set(${out_var} "${_rp_abs}/${_rest}" PARENT_SCOPE)
                return()
            endif()
        endforeach()
    elseif(ref MATCHES "^@(loader_path|executable_path)/(.+)$")
        if(EXISTS "${_exe_dir}/${CMAKE_MATCH_2}")
            set(${out_var} "${_exe_dir}/${CMAKE_MATCH_2}" PARENT_SCOPE)
        endif()
    elseif(EXISTS "${ref}")
        set(${out_var} "${ref}" PARENT_SCOPE)
    endif()
endfunction()

set(_bundled 0)
foreach(_line IN LISTS _link_lines)
    # Each dependency line is "\t<path> (compatibility ...)".
    if(NOT _line MATCHES "^\t(.+) \\(compatibility")
        continue()
    endif()
    set(_ref "${CMAKE_MATCH_1}")

    # Skip system libraries / SDK frameworks — never bundled.
    if(_ref MATCHES "^/usr/lib/" OR _ref MATCHES "^/System/"
       OR _ref MATCHES "^/Library/" OR _ref MATCHES "\\.framework/")
        continue()
    endif()

    _resolve_ref("${_ref}" _src)
    if(_src STREQUAL "")
        message(WARNING "macos_bundle_dylibs: could not resolve '${_ref}' — skipping (bundle may be incomplete)")
        continue()
    endif()

    # Name the bundled file after the reference (what dyld looks up), but copy
    # the symlink's real target content — most dylibs are versioned symlinks
    # (librtaudio.8.dylib -> librtaudio.8.0.0.dylib); a preserved symlink would
    # dangle in the bundle. `cmake -E copy` dereferences.
    get_filename_component(_base "${_ref}" NAME)
    get_filename_component(_real "${_src}" REALPATH)
    execute_process(COMMAND ${CMAKE_COMMAND} -E copy "${_real}" "${DEST}/${_base}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "macos_bundle_dylibs: copy failed for ${_real}")
    endif()
    execute_process(COMMAND install_name_tool -change
                        "${_ref}" "@executable_path/${_base}" "${EXE}"
                    RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "macos_bundle_dylibs: install_name_tool -change failed for ${_ref}")
    endif()
    math(EXPR _bundled "${_bundled} + 1")
endforeach()

# Belt-and-suspenders: ensure @executable_path is on the rpath so any @rpath/...
# reference we left untouched still resolves next to the exe. Duplicate-add is a
# benign error, so do not fail the build on it.
execute_process(COMMAND install_name_tool -add_rpath "@executable_path" "${EXE}"
                ERROR_QUIET RESULT_VARIABLE _ignored)

# install_name_tool invalidates the code signature; arm64 binaries must be
# (re-)signed to run. Ad-hoc sign the modified exe.
execute_process(COMMAND codesign --force --sign - "${EXE}"
                ERROR_QUIET RESULT_VARIABLE _ignored)

message(STATUS "macos_bundle_dylibs: bundled ${_bundled} dylib(s) next to ${EXE}")
