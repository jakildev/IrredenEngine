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
#       [DEFAULT_MODE <CODEGEN|EVAL>]   # default CODEGEN
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
# DEFAULT_MODE selects how systems without an explicit Lua
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

    # DEFAULT_MODE precedence — explicit param > IR_LUA_ECS_DEFAULT_MODE
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

# irreden_bundle_assets — populate the exe-relative runtime asset layout that
# every creation needs (the engine resolves data/, shaders/, scripts/ from the
# executable's directory; see IREngine::init). Factors the per-demo asset-copy
# boilerplate (DLL copy + data/shaders/scripts) into one call so it is defined
# once instead of hand-copied across every creation's CMakeLists.
#
# Usage:
#   irreden_bundle_assets(<target>
#       [SCRIPTS f1.lua f2.lua ...]      # copied into <exedir>/scripts/
#       [EXTRA_DIRS <src1> <dst1> ...]   # copy dir <src> -> <exedir>/<dst>
#   )
#
# Produces, next to the executable ($<TARGET_FILE_DIR:target>):
#   data/    <- engine/render/data + engine/data   (merged)
#   shaders/ <- engine/render/src/shaders          (.metal/.glsl source the
#               runtime path actually loads)
#   scripts/<f> for each SCRIPTS file (resolved from the caller's source dir)
#   <dst>/   for each EXTRA_DIRS (src dst) pair
# On Windows it also POST_BUILD-copies $<TARGET_RUNTIME_DLLS:target> next to the
# exe (inert off-Windows). Defines a `<target>Assets` custom target carrying the
# directory/script copies and wires add_dependencies(<target> <target>Assets);
# the layout matches the hand-written per-demo blocks byte-for-byte.
function(irreden_bundle_assets target)
    cmake_parse_arguments(IRBA "" "" "SCRIPTS;EXTRA_DIRS" ${ARGN})

    set(_exedir "$<TARGET_FILE_DIR:${target}>")

    # Windows runtime DLLs next to the exe. WIN32 (not IR_isWindows): the
    # latter is set PARENT_SCOPE only in scopes that called
    # IrredenEngine_setSystemCompileDefinitions, so it is unreliable inside a
    # standalone helper. COMMAND_EXPAND_LISTS expands the DLL generator-list.
    if(WIN32)
        add_custom_command(
            TARGET ${target}
            POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy -t ${_exedir} $<TARGET_RUNTIME_DLLS:${target}>
            COMMAND_EXPAND_LISTS
        )
    endif()

    # Asset directories: merge engine/render/data + engine/data into data/, and
    # the shader source tree into shaders/. copy_directory (not _if_different)
    # to match the existing per-demo Assets targets exactly.
    set(_asset_cmds
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/engine/render/data ${_exedir}/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/engine/data ${_exedir}/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${_exedir}/shaders
    )

    # Scripts -> <exedir>/scripts/<basename>. copy_if_different keeps the
    # per-script copy cheap on incremental builds (matches the demos' OUTPUT
    # form, which fed the Assets target's DEPENDS).
    list(APPEND _asset_cmds
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_exedir}/scripts)
    if(IRBA_SCRIPTS)
        foreach(_s IN LISTS IRBA_SCRIPTS)
            if(IS_ABSOLUTE "${_s}")
                set(_src "${_s}")
            else()
                set(_src "${CMAKE_CURRENT_SOURCE_DIR}/${_s}")
            endif()
            get_filename_component(_name "${_s}" NAME)
            list(APPEND _asset_cmds
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${_src} ${_exedir}/scripts/${_name})
        endforeach()
    endif()

    # EXTRA_DIRS: (src dst) pairs; dst is relative to the exe directory.
    if(IRBA_EXTRA_DIRS)
        list(LENGTH IRBA_EXTRA_DIRS _ed_len)
        math(EXPR _ed_mod "${_ed_len} % 2")
        if(NOT _ed_mod EQUAL 0)
            message(FATAL_ERROR
                "irreden_bundle_assets(${target}): EXTRA_DIRS needs <src> <dst> pairs")
        endif()
        math(EXPR _ed_last "${_ed_len} - 1")
        foreach(_i RANGE 0 ${_ed_last} 2)
            list(GET IRBA_EXTRA_DIRS ${_i} _ed_src)
            math(EXPR _j "${_i} + 1")
            list(GET IRBA_EXTRA_DIRS ${_j} _ed_dst)
            list(APPEND _asset_cmds
                COMMAND ${CMAKE_COMMAND} -E copy_directory
                    ${_ed_src} ${_exedir}/${_ed_dst})
        endforeach()
    endif()

    add_custom_target(${target}Assets ${_asset_cmds})
    add_dependencies(${target} ${target}Assets)
endfunction()

# irreden_package_target — assemble a self-contained, per-platform,
# double-clickable bundle (exe + data/ + shaders/ + scripts/ + runtime libs)
# and zip it to <target>-<platform>-<arch>.zip in the build dir. Consumes the
# exe-relative layout irreden_bundle_assets() already produced, so call that
# first. One-command invocation:
#   cmake --build <build-dir> --target <target>Package
#
# Relocatability is already handled by IREngine::init (cwd -> exe dir), so the
# unzipped folder runs by double-click on a clean box. Uses `cmake -E tar
# --format=zip`, NOT CPack: CPack's install()-prefix model fights the
# per-platform dep bundling and the exe-relative asset layout, and a custom
# target gives full control for a time-boxed packaging deliverable.
#
# Platform runtime-dep bundling (next to the exe in the staging dir):
#   Windows: $<TARGET_RUNTIME_DLLS> + the MinGW runtime trio (libgcc_s_seh-1,
#            libstdc++-6, libwinpthread-1), which TARGET_RUNTIME_DLLS omits.
#   Linux:   deps default to static (top-level CMakeLists.txt), so usually
#            nothing to bundle; $ORIGIN rpath covers any shared .so left.
#   macOS:   cmake/macos_bundle_dylibs.cmake copies the exe's non-system
#            dylibs next to it and rewrites the load commands to
#            @executable_path. NOTE: this is a SHALLOW (direct-dependency)
#            bundle — transitive Homebrew deps (e.g. ffmpeg's codec libs) are
#            NOT yet walked, so clean-box self-containment is follow-up work;
#            verify a macOS bundle via cross-host smoke / the human.
function(irreden_package_target target)
    set(_exedir "$<TARGET_FILE_DIR:${target}>")
    set(_pkg_root "${CMAKE_BINARY_DIR}/package")
    set(_stage "${_pkg_root}/${target}")
    set(_staged_exe "${_stage}/$<TARGET_FILE_NAME:${target}>")

    # platform-arch tag for the archive name (e.g. macos-arm64, linux-x86_64).
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _os)
    if(_os STREQUAL "darwin")
        set(_os "macos")
    endif()
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _arch)
    set(_zip "${CMAKE_BINARY_DIR}/${target}-${_os}-${_arch}.zip")

    # Clean staging dir + the exe-relative asset layout (already built by
    # <target>Assets, which we DEPEND on below).
    set(_pkg_cmds
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_stage}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_stage}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${_stage}/
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_exedir}/data ${_stage}/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_exedir}/shaders ${_stage}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_exedir}/scripts ${_stage}/scripts
    )

    if(WIN32)
        list(APPEND _pkg_cmds
            COMMAND ${CMAKE_COMMAND} -E copy -t ${_stage} $<TARGET_RUNTIME_DLLS:${target}>)
        # MinGW runtime trio is not in TARGET_RUNTIME_DLLS; pull it from the
        # compiler's bin dir (creations/CLAUDE.md gotcha).
        get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        list(APPEND _pkg_cmds
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${_mingw_bin}/libgcc_s_seh-1.dll
                ${_mingw_bin}/libstdc++-6.dll
                ${_mingw_bin}/libwinpthread-1.dll
                ${_stage})
    elseif(APPLE)
        list(APPEND _pkg_cmds
            COMMAND ${CMAKE_COMMAND}
                -DEXE=${_staged_exe} -DDEST=${_stage}
                -P ${PROJECT_SOURCE_DIR}/cmake/macos_bundle_dylibs.cmake)
    endif()

    # Zip from the package root so the archive holds a single top-level
    # <target>/ folder (unzip -> double-clickable bundle dir). `cmake -E chdir`
    # avoids a WORKING_DIRECTORY on the target itself, which make would cd into
    # before the staging dir exists. tar --format=zip needs CMake >= 3.18;
    # project min is 3.28, so no version bump.
    list(APPEND _pkg_cmds
        COMMAND ${CMAKE_COMMAND} -E chdir ${_pkg_root}
            ${CMAKE_COMMAND} -E tar cf ${_zip} --format=zip ${target})

    # Linux note: no runtime libs are staged because FetchContent deps default
    # to static there (top-level CMakeLists.txt), so the exe is self-contained.
    # If a Linux build ever enables BUILD_SHARED_LIBS, the staged exe would
    # need a `patchelf --set-rpath '$ORIGIN'` step + the shared .so copied in —
    # INSTALL_RPATH alone doesn't help, since manual-copy packaging bypasses
    # CMake's install() rpath rewrite.

    add_custom_target(${target}Package
        ${_pkg_cmds}
        DEPENDS ${target} ${target}Assets
        COMMENT "Packaging ${target} -> ${target}-${_os}-${_arch}.zip"
        COMMAND_EXPAND_LISTS
        VERBATIM)
endfunction()
