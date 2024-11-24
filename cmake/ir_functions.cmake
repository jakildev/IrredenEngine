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
