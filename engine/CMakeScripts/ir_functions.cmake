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
