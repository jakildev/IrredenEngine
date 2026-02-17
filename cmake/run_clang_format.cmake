if(NOT DEFINED CLANG_FORMAT_BIN OR CLANG_FORMAT_BIN STREQUAL "")
    message(FATAL_ERROR "CLANG_FORMAT_BIN is required.")
endif()
string(REPLACE "\"" "" CLANG_FORMAT_BIN "${CLANG_FORMAT_BIN}")

if(NOT DEFINED QUALITY_FILE_LIST OR QUALITY_FILE_LIST STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILE_LIST is required.")
endif()
string(REPLACE "\"" "" QUALITY_FILE_LIST "${QUALITY_FILE_LIST}")

include("${QUALITY_FILE_LIST}")
if(NOT DEFINED QUALITY_FILES OR QUALITY_FILES STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILES is empty. No files to process.")
endif()

if(NOT DEFINED FORMAT_MODE OR FORMAT_MODE STREQUAL "")
    set(FORMAT_MODE "fix")
endif()

set(failed_files "")
set(file_count 0)

foreach(file_path IN LISTS QUALITY_FILES)
    math(EXPR file_count "${file_count} + 1")
    if(FORMAT_MODE STREQUAL "check")
        execute_process(
            COMMAND "${CLANG_FORMAT_BIN}" --dry-run --Werror --style=file "${file_path}"
            RESULT_VARIABLE format_result
            ERROR_VARIABLE format_error
        )
    else()
        execute_process(
            COMMAND "${CLANG_FORMAT_BIN}" -i --style=file "${file_path}"
            RESULT_VARIABLE format_result
            ERROR_VARIABLE format_error
        )
    endif()

    if(NOT format_result EQUAL 0)
        list(APPEND failed_files "${file_path}")
        if(NOT format_error STREQUAL "")
            message(STATUS "${format_error}")
        endif()
    endif()
endforeach()

if(FORMAT_MODE STREQUAL "check")
    message(STATUS "clang-format checked ${file_count} file(s).")
else()
    message(STATUS "clang-format formatted ${file_count} file(s).")
endif()

if(failed_files)
    list(JOIN failed_files "\n  - " failed_files_joined)
    if(FORMAT_MODE STREQUAL "check")
        message(FATAL_ERROR
            "clang-format check failed for file(s):\n"
            "  - ${failed_files_joined}\n"
            "Run target 'format' to apply formatting."
        )
    else()
        message(FATAL_ERROR
            "clang-format failed for file(s):\n"
            "  - ${failed_files_joined}"
        )
    endif()
endif()
