# macos_bundle_dylibs.cmake — discover and stage a complete Mach-O closure.
#
# Bundle mode:
#   cmake -DEXE=<staged exe> -DDEST=<staging dir> [-DOTOOL=<path>]
#         [-DINSTALL_NAME_TOOL=<path>] [-DCODESIGN=<path>]
#         -P macos_bundle_dylibs.cmake
#
# Report mode, used by scripts/package-verify.py:
#   cmake -DEXE=<built exe> -DOTOOL=<path> -DREPORT_ONLY=ON
#         -P macos_bundle_dylibs.cmake
#
# System libraries under /usr/lib and /System/Library remain supplied by
# macOS. Every other resolved dependency is copied next to the executable.

cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED EXE OR NOT EXISTS "${EXE}")
    message(FATAL_ERROR "macos_bundle_dylibs: -DEXE must name an executable")
endif()
if(NOT REPORT_ONLY AND (NOT DEFINED DEST OR NOT IS_DIRECTORY "${DEST}"))
    message(FATAL_ERROR "macos_bundle_dylibs: -DDEST must name the staging directory")
endif()

if(NOT OTOOL)
    find_program(OTOOL otool REQUIRED)
endif()
if(NOT REPORT_ONLY)
    if(NOT INSTALL_NAME_TOOL)
        find_program(INSTALL_NAME_TOOL install_name_tool REQUIRED)
    endif()
    if(NOT CODESIGN)
        find_program(CODESIGN codesign REQUIRED)
    endif()
endif()

set(CMAKE_GET_RUNTIME_DEPENDENCIES_PLATFORM "macos+macho")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL "otool")
set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND "${OTOOL}")

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES "${EXE}"
    POST_EXCLUDE_REGEXES "^/usr/lib/" "^/System/Library/"
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    CONFLICTING_DEPENDENCIES_PREFIX _conflict)

foreach(_path IN LISTS _resolved)
    message(STATUS "R ${_path}")
endforeach()
foreach(_name IN LISTS _unresolved)
    message(STATUS "U ${_name}")
endforeach()
foreach(_name IN LISTS _conflict_FILENAMES)
    message(STATUS "C ${_name}|${_conflict_${_name}}")
endforeach()

if(_unresolved)
    message(FATAL_ERROR
        "macos_bundle_dylibs: unresolved dependencies: ${_unresolved}")
endif()
if(_conflict_FILENAMES)
    message(FATAL_ERROR
        "macos_bundle_dylibs: conflicting dependency names: ${_conflict_FILENAMES}")
endif()
if(REPORT_ONLY)
    return()
endif()

set(_staged_libraries "")
foreach(_source IN LISTS _resolved)
    get_filename_component(_leaf "${_source}" NAME)
    get_filename_component(_real "${_source}" REALPATH)
    set(_staged "${DEST}/${_leaf}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy "${_real}" "${_staged}"
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "macos_bundle_dylibs: copy failed for ${_real}")
    endif()
    list(APPEND _staged_libraries "${_staged}")
endforeach()

function(_read_dependencies macho output_var)
    execute_process(
        COMMAND "${OTOOL}" -L "${macho}"
        OUTPUT_VARIABLE _linked
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "macos_bundle_dylibs: otool -L failed on ${macho}")
    endif()
    string(REPLACE "\n" ";" _lines "${_linked}")
    set(_dependencies "")
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "^.(.+) \\(compatibility")
            list(APPEND _dependencies "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    set(${output_var} "${_dependencies}" PARENT_SCOPE)
endfunction()

function(_read_install_id macho output_var)
    execute_process(
        COMMAND "${OTOOL}" -D "${macho}"
        OUTPUT_VARIABLE _id_output
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        set(${output_var} "" PARENT_SCOPE)
        return()
    endif()
    string(REPLACE "\n" ";" _id_lines "${_id_output}")
    list(LENGTH _id_lines _id_count)
    if(_id_count GREATER 1)
        list(GET _id_lines 1 _id)
        set(${output_var} "${_id}" PARENT_SCOPE)
    else()
        set(${output_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(_read_rpaths macho output_var)
    execute_process(
        COMMAND "${OTOOL}" -l "${macho}"
        OUTPUT_VARIABLE _commands
        RESULT_VARIABLE _result)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR "macos_bundle_dylibs: otool -l failed on ${macho}")
    endif()
    string(REPLACE "\n" ";" _lines "${_commands}")
    set(_rpaths "")
    set(_expect_path FALSE)
    foreach(_line IN LISTS _lines)
        if(_line MATCHES "cmd LC_RPATH")
            set(_expect_path TRUE)
        elseif(_expect_path AND _line MATCHES "path (.+) \\(offset")
            list(APPEND _rpaths "${CMAKE_MATCH_1}")
            set(_expect_path FALSE)
        endif()
    endforeach()
    set(${output_var} "${_rpaths}" PARENT_SCOPE)
endfunction()

set(_machos "${EXE};${_staged_libraries}")
foreach(_macho IN LISTS _machos)
    _read_install_id("${_macho}" _install_id)
    _read_dependencies("${_macho}" _dependencies)
    foreach(_reference IN LISTS _dependencies)
        if("${_reference}" STREQUAL "${_install_id}"
           OR _reference MATCHES "^/usr/lib/"
           OR _reference MATCHES "^/System/Library/")
            continue()
        endif()
        get_filename_component(_leaf "${_reference}" NAME)
        if(NOT EXISTS "${DEST}/${_leaf}")
            message(FATAL_ERROR
                "macos_bundle_dylibs: ${_macho} references unstaged ${_reference}")
        endif()
        execute_process(
            COMMAND "${INSTALL_NAME_TOOL}" -change "${_reference}"
                    "@loader_path/${_leaf}" "${_macho}"
            RESULT_VARIABLE _result
            ERROR_VARIABLE _error)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR
                "macos_bundle_dylibs: failed to rewrite ${_reference} in ${_macho}: ${_error}")
        endif()
    endforeach()

    if(NOT "${_macho}" STREQUAL "${EXE}")
        get_filename_component(_leaf "${_macho}" NAME)
        execute_process(
            COMMAND "${INSTALL_NAME_TOOL}" -id "@loader_path/${_leaf}" "${_macho}"
            RESULT_VARIABLE _result
            ERROR_VARIABLE _error)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR
                "macos_bundle_dylibs: failed to set id for ${_macho}: ${_error}")
        endif()
    endif()

    _read_rpaths("${_macho}" _rpaths)
    set(_has_local_rpath FALSE)
    foreach(_rpath IN LISTS _rpaths)
        if(_rpath STREQUAL "@loader_path")
            set(_has_local_rpath TRUE)
        elseif(_rpath MATCHES "^/opt/homebrew(/|$)" OR
               _rpath MATCHES "^/usr/local(/|$)")
            execute_process(
                COMMAND "${INSTALL_NAME_TOOL}" -delete_rpath "${_rpath}" "${_macho}"
                RESULT_VARIABLE _result
                ERROR_VARIABLE _error)
            if(NOT _result EQUAL 0)
                message(FATAL_ERROR
                    "macos_bundle_dylibs: failed to remove rpath ${_rpath} "
                    "from ${_macho}: ${_error}")
            endif()
        endif()
    endforeach()
    if(NOT _has_local_rpath)
        execute_process(
            COMMAND "${INSTALL_NAME_TOOL}" -add_rpath "@loader_path" "${_macho}"
            RESULT_VARIABLE _result
            ERROR_VARIABLE _error)
        if(NOT _result EQUAL 0)
            message(FATAL_ERROR
                "macos_bundle_dylibs: failed to add local rpath to ${_macho}: ${_error}")
        endif()
    endif()
endforeach()

foreach(_library IN LISTS _staged_libraries)
    execute_process(
        COMMAND "${CODESIGN}" --force --sign - "${_library}"
        RESULT_VARIABLE _result
        ERROR_VARIABLE _error)
    if(NOT _result EQUAL 0)
        message(FATAL_ERROR
            "macos_bundle_dylibs: codesign failed for ${_library}: ${_error}")
    endif()
endforeach()
execute_process(
    COMMAND "${CODESIGN}" --force --sign - "${EXE}"
    RESULT_VARIABLE _result
    ERROR_VARIABLE _error)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "macos_bundle_dylibs: codesign failed for ${EXE}: ${_error}")
endif()

list(LENGTH _staged_libraries _bundled)
message(STATUS "macos_bundle_dylibs: bundled ${_bundled} dylib(s) next to ${EXE}")
