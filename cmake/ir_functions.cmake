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
#       [DEFAULT_MODE <CODEGEN|EVAL>]   # T-108; default CODEGEN
#   )
#
# Behaviour:
#   - Adds a custom command that runs `ir_lua_codegen` whenever any of the
#     SOURCES change, regenerating OUTPUT_HPP.
#   - Adds OUTPUT_HPP to <target>'s sources so CMake tracks the dependency.
#   - Adds OUTPUT_HPP's parent directory to <target>'s include path.
#   - The custom command's DEPENDS list already includes ir_lua_codegen, so
#     the codegen binary is built first on a clean tree without a separate
#     add_dependencies() edge.
#
# T-108: DEFAULT_MODE selects how systems without an explicit Lua
# `mode = "..."` field are emitted. CODEGEN (default) emits a typed C++
# `IRSystem::createSystem<...>` per system; EVAL skips C++ emission and
# the system registers at runtime via the existing Lua-driven path. The
# emitted header exports `IRScript::CodegenRegistry::kDefaultEcsMode`
# matching this value so runtime dispatch via
# `LuaScript::setEcsDefaultMode()` agrees with the build-time decision.
# Per-creation override via the `IR_LUA_ECS_DEFAULT_MODE` cache variable
# (set in the creation's CMakeLists.txt or on the cmake command line)
# takes precedence when DEFAULT_MODE is omitted.
#
# All paths are resolved relative to the caller's CMAKE_CURRENT_SOURCE_DIR
# unless absolute. The generated header is regenerated on Lua-source change
# but not on tool-source change (the codegen tool target's link dependency
# already handles tool rebuilds; CMake re-runs the custom command when its
# input checksums shift).
function(
    irreden_lua_codegen target
)
    cmake_parse_arguments(IRLC "" "OUTPUT_HPP;DEFAULT_MODE" "SOURCES" ${ARGN})
    if(NOT IRLC_OUTPUT_HPP)
        message(FATAL_ERROR "irreden_lua_codegen: OUTPUT_HPP is required")
    endif()
    if(NOT IRLC_SOURCES)
        message(FATAL_ERROR "irreden_lua_codegen: SOURCES is required")
    endif()

    # T-108: DEFAULT_MODE precedence — explicit param > IR_LUA_ECS_DEFAULT_MODE
    # cache var > CODEGEN. The cache var lets `cmake -DIR_LUA_ECS_DEFAULT_MODE=EVAL`
    # flip a build flavor without editing creation CMakeLists.
    if(NOT IRLC_DEFAULT_MODE)
        if(DEFINED IR_LUA_ECS_DEFAULT_MODE)
            set(IRLC_DEFAULT_MODE "${IR_LUA_ECS_DEFAULT_MODE}")
        else()
            set(IRLC_DEFAULT_MODE "CODEGEN")
        endif()
    endif()
    if(NOT IRLC_DEFAULT_MODE STREQUAL "CODEGEN" AND NOT IRLC_DEFAULT_MODE STREQUAL "EVAL")
        message(FATAL_ERROR
            "irreden_lua_codegen: DEFAULT_MODE must be CODEGEN or EVAL "
            "(got '${IRLC_DEFAULT_MODE}')"
        )
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

    # CMake 3.31+ added a CODEGEN positional keyword to add_custom_command;
    # any bare "CODEGEN" token in the call (even after variable expansion
    # inside COMMAND) trips its policy gate and aborts configure. Pass the
    # value lowercased to the CLI tool so the literal "CODEGEN" never
    # appears in the add_custom_command argument list.
    string(TOLOWER "${IRLC_DEFAULT_MODE}" _mode_lower)

    add_custom_command(
        OUTPUT "${IRLC_OUTPUT_HPP}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_output_dir}"
        COMMAND $<TARGET_FILE:ir_lua_codegen>
            --out "${IRLC_OUTPUT_HPP}"
            "--default-mode=${_mode_lower}"
            ${_resolved_sources}
        DEPENDS ir_lua_codegen ${_resolved_sources}
        COMMENT "lua_codegen [${_mode_lower}]: ${IRLC_OUTPUT_HPP}"
        VERBATIM
    )

    target_sources(${target} PRIVATE "${IRLC_OUTPUT_HPP}")
    target_include_directories(${target} PRIVATE "${_output_dir}")
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