# windows_dll_walk.cmake — print the transitive PE import resolution of one
# executable, for scripts/package-verify.py to grade.
#
#   cmake -DEXE=<exe> -DOBJDUMP=<objdump> -DDIRS=<dir;dir...>
#         [-DLIBRARY_DIR=<dir>] [-DSKIP_REGEXES=<re;re...>]
#         -P windows_dll_walk.cmake
#
# LIBRARY_DIR adds every *.dll in it as a walk root beside EXE, so a bundled
# DLL nothing imports is still checked. SKIP_REGEXES are pre-excluded by import
# name, which also stops the walk from recursing into them. Only DIRS and the
# Windows system directories are searched; the caller's PATH is not.
#
# Output, one STATUS line per item:
#   -- R <resolved path>
#   -- U <unresolved import name>
#   -- C <import name>|<path;path...>
#
# file(GET_RUNTIME_DEPENDENCIES) is script-mode only — do not include() this
# file from a project.

# Script mode starts with no policies set.
cmake_minimum_required(VERSION 3.28)

foreach(_req EXE OBJDUMP DIRS)
    if("${${_req}}" STREQUAL "")
        message(FATAL_ERROR "windows_dll_walk: -D${_req} is required")
    endif()
endforeach()

set(_libraries "")
if(LIBRARY_DIR)
    file(GLOB _libraries "${LIBRARY_DIR}/*.dll")
endif()

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "windows+pe")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "objdump")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OBJDUMP}")

# api-ms-*/ext-ms-* are API-set contracts the loader maps onto system DLLs;
# no file by that name exists to resolve.
file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    LIBRARIES ${_libraries}
    DIRECTORIES ${DIRS}
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-" ${SKIP_REGEXES}
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _conflict
)

foreach(_path IN LISTS _resolved)
    message(STATUS "R ${_path}")
endforeach()
foreach(_name IN LISTS _unresolved)
    message(STATUS "U ${_name}")
endforeach()
foreach(_name IN LISTS _conflict_FILENAMES)
    message(STATUS "C ${_name}|${_conflict_${_name}}")
endforeach()
