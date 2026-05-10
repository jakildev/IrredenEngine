function(
    IrredenEngine_applyPatchOnce
    packageName
    patchName
    patchFile
)
    if (NOT ${packageName}_POPULATED)
        FetchContent_Populate(${packageName})
        set(markerFileName "${${packageName}_SOURCE_DIR}/irreden/${patchName}_patch_applied.txt")
        if (NOT EXISTS ${markerFileName})
            execute_process(
                COMMAND git apply --reject --ignore-whitespace ${patchFile}
                WORKING_DIRECTORY ${${packageName}_SOURCE_DIR}
                RESULT_VARIABLE PATCH_RESULT
            )
            if (PATCH_RESULT EQUAL 0)
                message("${patchName} patch applied successfully.")
            else()
                message("${patchName} patch failed to apply, SHOULD ABORT HERE.")
            endif()
        file(WRITE ${markerFileName} "Patch applied")
        else()
            message("${patchName} skipped, already marked as applied. To reapply, delete ${markerFileName}.")
        endif()
        add_subdirectory(
            ${${packageName}_SOURCE_DIR}
            ${${packageName}_BINARY_DIR}
            EXCLUDE_FROM_ALL
        )
    endif()
endfunction()

# function(
#     IR_isWindows
# )
#     if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
#         set(${result} TRUE PARENT_SCOPE)
#     else()
#         set(${result} FALSE PARENT_SCOPE)
#     endif()
# endfunction()

# function(
#     IR_isDarwin
# )
#     if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
#         set(${result} TRUE PARENT_SCOPE)
#     else()
#         set(${result} FALSE PARENT_SCOPE)
#     endif()
# endfunction()

# function(
#     IR_isLinux
# )
#     if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
#         set(${result} TRUE PARENT_SCOPE)
#     else()
#         set(${result} FALSE PARENT_SCOPE)
#     endif()
# endfunction()

function(
    IrredenEngine_setSystemCompileDefinitions
    targetName
)
    # let the preprocessor know about the system name
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message("IrredenEngine: Linux system detected.")
        set(IR_isLinux TRUE PARENT_SCOPE)
        target_compile_definitions(${targetName} PUBLIC "IS_LINUX")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        message("IrredenEngine: Darwin system detected.")
        set(IR_isDarwin TRUE PARENT_SCOPE)
        target_compile_definitions(${targetName} PUBLIC "IS_MACOS")
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        message("IrredenEngine: Windows system detected.")
        set(IR_isWindows TRUE PARENT_SCOPE)
        target_compile_definitions(${targetName} PUBLIC "IS_WINDOWS")
    endif()
endfunction()

function(
    IrredenEngine_setGraphicsBackendCompileDefinitions
    targetName
)
    if(IRREDEN_GRAPHICS_BACKEND STREQUAL "OPENGL")
        target_compile_definitions(${targetName} PUBLIC "IR_GRAPHICS_OPENGL")
    elseif(IRREDEN_GRAPHICS_BACKEND STREQUAL "METAL")
        target_compile_definitions(${targetName} PUBLIC "IR_GRAPHICS_METAL")
    elseif(IRREDEN_GRAPHICS_BACKEND STREQUAL "VULKAN")
        target_compile_definitions(${targetName} PUBLIC "IR_GRAPHICS_VULKAN")
    else()
        message(FATAL_ERROR "Unsupported IRREDEN_GRAPHICS_BACKEND='${IRREDEN_GRAPHICS_BACKEND}'")
    endif()
endfunction()

function(
    IrredenEngine_copyLuaFiles
)
    # Set the output directory for copied files
    set(OUTPUT_DIR "${PROJECT_BINARY_DIR}/lua_files")

    # Find all .lua files recursively from the current source directory
    file(GLOB_RECURSE LUA_FILES "${CMAKE_SOURCE_DIR}/*.lua")

    # Create a custom command for each Lua file to copy it to the build directory
    foreach(LUA_FILE ${LUA_FILES})
        # Get relative path and destination file
        file(RELATIVE_PATH REL_PATH ${CMAKE_SOURCE_DIR} ${LUA_FILE})
        set(DEST_FILE "${OUTPUT_DIR}/${REL_PATH}")

        # Create any necessary directories
        add_custom_command(
            OUTPUT "${DEST_FILE}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${DEST_FILE}>"
            COMMAND ${CMAKE_COMMAND} -E copy "${LUA_FILE}" "${DEST_FILE}"
            COMMENT "Copying ${LUA_FILE} to ${DEST_FILE}"
            VERBATIM
        )
 
        # Add to target
        list(APPEND COPY_COMMANDS "${DEST_FILE}")
    endforeach()

    # Add a custom target that depends on all copy commands
    add_custom_target(copy_all_lua_files ALL DEPENDS ${COPY_COMMANDS})
endfunction()

# irreden_lua_codegen — build a C++ header from one or more Lua component
# schemas. Wraps cmake/lua_codegen/main.cpp's CLI as a CMake-friendly helper.
#
# Usage:
#   irreden_lua_codegen(<target>
#       SOURCES <input1.lua> [input2.lua ...]
#       OUTPUT_HPP <path/to/generated.hpp>
#   )
#
# Behaviour:
#   - Adds a custom command that runs `ir_lua_codegen` whenever any of the
#     SOURCES change, regenerating OUTPUT_HPP.
#   - Adds OUTPUT_HPP to <target>'s sources so CMake tracks the dependency.
#   - Adds OUTPUT_HPP's parent directory to <target>'s include path.
#   - Adds an explicit dependency on the ir_lua_codegen tool target so the
#     codegen binary is built first on a clean tree.
#
# All paths are resolved relative to the caller's CMAKE_CURRENT_SOURCE_DIR
# unless absolute. The generated header is regenerated on Lua-source change
# but not on tool-source change (the codegen tool target's link dependency
# already handles tool rebuilds; CMake re-runs the custom command when its
# input checksums shift).
function(
    irreden_lua_codegen target
)
    cmake_parse_arguments(IRLC "" "OUTPUT_HPP" "SOURCES" ${ARGN})
    if(NOT IRLC_OUTPUT_HPP)
        message(FATAL_ERROR "irreden_lua_codegen: OUTPUT_HPP is required")
    endif()
    if(NOT IRLC_SOURCES)
        message(FATAL_ERROR "irreden_lua_codegen: SOURCES is required")
    endif()

    # Resolve OUTPUT_HPP to an absolute path so add_custom_command's OUTPUT
    # is unambiguous regardless of caller cwd.
    if(NOT IS_ABSOLUTE "${IRLC_OUTPUT_HPP}")
        set(IRLC_OUTPUT_HPP "${CMAKE_CURRENT_BINARY_DIR}/${IRLC_OUTPUT_HPP}")
    endif()

    # Resolve sources to absolute paths so the custom command's DEPENDS list
    # is stable across configures.
    set(_resolved_sources "")
    foreach(_src IN LISTS IRLC_SOURCES)
        if(NOT IS_ABSOLUTE "${_src}")
            set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
        endif()
        list(APPEND _resolved_sources "${_src}")
    endforeach()

    get_filename_component(_output_dir "${IRLC_OUTPUT_HPP}" DIRECTORY)

    add_custom_command(
        OUTPUT "${IRLC_OUTPUT_HPP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_output_dir}"
        COMMAND $<TARGET_FILE:ir_lua_codegen>
            --out "${IRLC_OUTPUT_HPP}"
            ${_resolved_sources}
        DEPENDS ir_lua_codegen ${_resolved_sources}
        COMMENT "lua_codegen: ${IRLC_OUTPUT_HPP}"
        VERBATIM
    )

    target_sources(${target} PRIVATE "${IRLC_OUTPUT_HPP}")
    target_include_directories(${target} PRIVATE "${_output_dir}")
    add_dependencies(${target} ir_lua_codegen)
endfunction()

# copyDLL copies dll file on windows build into dest path
function(
    IR_copyDLL
    target
    dllName
    sourceDir
)
    if(IR_isWindows)
        cmake_path(APPEND tempFullSourcePath ${sourceDir} ${dllName}.dll)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
                ${tempFullSourcePath}
                "$<TARGET_FILE_DIR:${target}>/${dllName}.dll"
        )
    endif()
endfunction()