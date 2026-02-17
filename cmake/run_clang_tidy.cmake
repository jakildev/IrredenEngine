if(NOT DEFINED CLANG_TIDY_BIN OR CLANG_TIDY_BIN STREQUAL "")
    message(FATAL_ERROR "CLANG_TIDY_BIN is required.")
endif()
string(REPLACE "\"" "" CLANG_TIDY_BIN "${CLANG_TIDY_BIN}")

if(NOT DEFINED BUILD_DIR OR BUILD_DIR STREQUAL "")
    message(FATAL_ERROR "BUILD_DIR is required.")
endif()
string(REPLACE "\"" "" BUILD_DIR "${BUILD_DIR}")

if(NOT DEFINED PROJECT_ROOT OR PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()
string(REPLACE "\"" "" PROJECT_ROOT "${PROJECT_ROOT}")

if(NOT DEFINED QUALITY_FILE_LIST OR QUALITY_FILE_LIST STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILE_LIST is required.")
endif()
string(REPLACE "\"" "" QUALITY_FILE_LIST "${QUALITY_FILE_LIST}")

include("${QUALITY_FILE_LIST}")
if(NOT DEFINED QUALITY_FILES OR QUALITY_FILES STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILES is empty. No files to lint.")
endif()

file(TO_CMAKE_PATH "${BUILD_DIR}" build_dir_normalized)
set(compile_commands_file "${build_dir_normalized}/compile_commands.json")
if(NOT EXISTS "${compile_commands_file}")
    message(FATAL_ERROR
        "compile_commands.json not found at ${compile_commands_file}. "
        "Reconfigure with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON."
    )
endif()

set(failed_files "")
set(file_count 0)
file(READ "${compile_commands_file}" compile_commands_content)

foreach(file_path IN LISTS QUALITY_FILES)
    file(TO_CMAKE_PATH "${file_path}" normalized_file_path)
    if(normalized_file_path MATCHES "/(build|_deps|third_party)/")
        continue()
    endif()
    if(NOT normalized_file_path MATCHES "\\.(c|cc|cxx|cpp)$")
        continue()
    endif()
    string(FIND "${compile_commands_content}" "${normalized_file_path}" file_in_compile_commands)
    if(file_in_compile_commands EQUAL -1)
        continue()
    endif()

    math(EXPR file_count "${file_count} + 1")
    execute_process(
        COMMAND
            "${CLANG_TIDY_BIN}"
            -p "${build_dir_normalized}"
            "--config-file=${PROJECT_ROOT}/.clang-tidy"
            --quiet
            "--system-headers=0"
            --extra-arg=-DIR_RELEASE
            --extra-arg=-D_USE_MATH_DEFINES
            "${normalized_file_path}"
        RESULT_VARIABLE tidy_result
        ERROR_VARIABLE tidy_error
    )

    if(NOT tidy_result EQUAL 0)
        list(APPEND failed_files "${normalized_file_path}")
        if(NOT tidy_error STREQUAL "")
            message(STATUS "${tidy_error}")
        endif()
    endif()
endforeach()

message(STATUS "clang-tidy checked ${file_count} file(s).")

if(failed_files)
    list(JOIN failed_files "\n  - " failed_files_joined)
    message(FATAL_ERROR
        "clang-tidy failed for file(s):\n"
        "  - ${failed_files_joined}"
    )
endif()
