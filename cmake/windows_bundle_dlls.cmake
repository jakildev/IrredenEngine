# windows_bundle_dlls.cmake — make a staged Windows executable self-contained.
#
# Invoked by irreden_package_target() (cmake/ir_functions.cmake) as:
#   cmake -DEXE=<staged exe> -DDEST=<staging dir> -DOBJDUMP=<objdump>
#         -DRUNTIME_DIR=<target's exe dir> [-DFFMPEG_BIN=<ffmpeg root>/bin]
#         -DCOMPILER_BIN=<compiler bin dir> -P windows_bundle_dlls.cmake
#
# Copies the executable's complete non-system PE import closure (transitive,
# via file(GET_RUNTIME_DEPENDENCIES)) flat next to it, so the unzipped folder
# runs with only the Windows system directories on PATH. Search order is
# RUNTIME_DIR (CMake-built DLLs), FFMPEG_BIN, then COMPILER_BIN (MinGW runtime
# and MSYS2 packages); the caller's PATH is never consulted. DLLs resolved
# under %SystemRoot% ship with the OS and are not bundled.
#
# An unresolved import or two different files claiming one DLL name is fatal:
# the caller zips only after this script succeeds, so a failure never leaves
# an archive that looks complete.
#
# file(GET_RUNTIME_DEPENDENCIES) is script-mode only — do not include() this
# file from a project.

# Script mode starts with no policies set; IN_LIST needs CMP0057.
cmake_minimum_required(VERSION 3.28)

foreach(_req EXE DEST OBJDUMP RUNTIME_DIR COMPILER_BIN)
    if("${${_req}}" STREQUAL "")
        message(FATAL_ERROR "windows_bundle_dlls: -D${_req} is required")
    endif()
endforeach()
if(NOT EXISTS "${EXE}")
    message(FATAL_ERROR "windows_bundle_dlls: staged exe not found: ${EXE}")
endif()

set(_sysroot "$ENV{SystemRoot}")
if(NOT _sysroot)
    message(FATAL_ERROR "windows_bundle_dlls: SystemRoot is not set in the environment")
endif()

# The resolver reports one system DLL under several spellings
# ("C:/Windows/system32/x.dll", "C:\WINDOWS\system32/x.dll"), so match the
# root case- and separator-insensitively. One bracket per character keeps
# regex metacharacters in the path literal.
file(TO_CMAKE_PATH "${_sysroot}" _sysroot)
string(LENGTH "${_sysroot}" _len)
math(EXPR _last "${_len} - 1")
set(_sysroot_re "^")
foreach(_i RANGE ${_last})
    string(SUBSTRING "${_sysroot}" ${_i} 1 _c)
    string(TOLOWER "${_c}" _lo)
    string(TOUPPER "${_c}" _up)
    if(_c STREQUAL "/")
        string(APPEND _sysroot_re "[\\/]")
    elseif(_lo STREQUAL _up)
        string(APPEND _sysroot_re "[${_c}]")
    else()
        string(APPEND _sysroot_re "[${_lo}${_up}]")
    endif()
endforeach()
string(APPEND _sysroot_re "[\\/]")

set(_dirs "${RUNTIME_DIR}")
if(FFMPEG_BIN)
    list(APPEND _dirs "${FFMPEG_BIN}")
endif()
list(APPEND _dirs "${COMPILER_BIN}")

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

# api-ms-*/ext-ms-* are API-set contracts the loader maps onto system DLLs;
# no file by that name exists to resolve. A post-excluded (system) DLL is
# also not recursed into.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    DIRECTORIES ${_dirs}
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "${_sysroot_re}"
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _conflict
)

if(_unresolved)
    list(JOIN _unresolved "\n  " _msg)
    message(FATAL_ERROR
        "windows_bundle_dlls: unresolved imports (searched ${_dirs}):\n  ${_msg}")
endif()
if(_conflict_FILENAMES)
    set(_msg "")
    foreach(_name IN LISTS _conflict_FILENAMES)
        string(APPEND _msg "\n  ${_name}: ${_conflict_${_name}}")
    endforeach()
    message(FATAL_ERROR "windows_bundle_dlls: one DLL name resolves to several files:${_msg}")
endif()

# DLL names are case-insensitive: compare lowercased, copy under the resolved
# on-disk spelling.
set(_seen_names "")
set(_seen_paths "")
foreach(_dll IN LISTS _resolved)
    file(TO_CMAKE_PATH "${_dll}" _dll)
    string(TOLOWER "${_dll}" _path_key)
    if(_path_key IN_LIST _seen_paths)
        continue()
    endif()
    get_filename_component(_name "${_dll}" NAME)
    string(TOLOWER "${_name}" _name_key)
    list(FIND _seen_names "${_name_key}" _idx)
    if(NOT _idx EQUAL -1)
        list(GET _seen_paths ${_idx} _other)
        message(FATAL_ERROR
            "windows_bundle_dlls: ${_name} resolves to both ${_other} and ${_dll}")
    endif()
    list(APPEND _seen_names "${_name_key}")
    list(APPEND _seen_paths "${_path_key}")
    file(COPY_FILE "${_dll}" "${DEST}/${_name}" ONLY_IF_DIFFERENT)
endforeach()

list(LENGTH _seen_names _bundled)
message(STATUS "windows_bundle_dlls: bundled ${_bundled} DLL(s) next to ${EXE}")
