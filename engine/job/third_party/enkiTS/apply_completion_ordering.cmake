if(NOT IS_DIRECTORY "${ENKITS_SOURCE_DIR}")
    message(FATAL_ERROR "enkiTS completion ordering requires ENKITS_SOURCE_DIR")
endif()
set(_completion_patch "${CMAKE_CURRENT_LIST_DIR}/completion_ordering.patch")
find_package(Git REQUIRED)
get_filename_component(_completion_source "${ENKITS_SOURCE_DIR}" REALPATH)
# Archives have no .git directory. Do not discover an enclosing engine checkout
# or honor inherited Git routing variables when applying dependency-relative paths.
set(_completion_git "${CMAKE_COMMAND}" -E env
    --unset=GIT_DIR --unset=GIT_WORK_TREE --unset=GIT_INDEX_FILE
    "GIT_CEILING_DIRECTORIES=${_completion_source}" "${GIT_EXECUTABLE}")

execute_process(
    COMMAND ${_completion_git} apply --reverse --check "${_completion_patch}"
    WORKING_DIRECTORY "${ENKITS_SOURCE_DIR}"
    RESULT_VARIABLE _reverse_result
    OUTPUT_QUIET ERROR_QUIET
)
if(_reverse_result EQUAL 0)
    return()
endif()

execute_process(
    COMMAND ${_completion_git} apply --check "${_completion_patch}"
    WORKING_DIRECTORY "${ENKITS_SOURCE_DIR}"
    RESULT_VARIABLE _check_result
    ERROR_VARIABLE _check_error
)
if(NOT _check_result EQUAL 0)
    message(FATAL_ERROR "enkiTS completion ordering patch is incompatible: ${_check_error}")
endif()

# Without --reject git applies all hunks atomically; an incompatible source
# must never compile with only part of the completion handshake repaired.
execute_process(
    COMMAND ${_completion_git} apply "${_completion_patch}"
    WORKING_DIRECTORY "${ENKITS_SOURCE_DIR}"
    RESULT_VARIABLE _apply_result
    ERROR_VARIABLE _apply_error
)
if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "enkiTS completion ordering patch failed: ${_apply_error}")
endif()
execute_process(
    COMMAND ${_completion_git} apply --reverse --check "${_completion_patch}"
    WORKING_DIRECTORY "${ENKITS_SOURCE_DIR}"
    RESULT_VARIABLE _verify_result
    ERROR_VARIABLE _verify_error
)
if(NOT _verify_result EQUAL 0)
    message(FATAL_ERROR "enkiTS completion ordering verification failed: ${_verify_error}")
endif()
