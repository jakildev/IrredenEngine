option(IRREDEN_ENABLE_QUALITY_TOOLS "Enable formatter and linter targets." ON)

function(irreden_collect_quality_files out_var)
    set(search_roots
        "${PROJECT_SOURCE_DIR}/engine"
        "${PROJECT_SOURCE_DIR}/creations"
        "${PROJECT_SOURCE_DIR}/test"
    )

    set(globbed_files "")
    foreach(root IN LISTS search_roots)
        if(EXISTS "${root}")
            file(GLOB_RECURSE root_files CONFIGURE_DEPENDS
                "${root}/*.h"
                "${root}/*.hpp"
                "${root}/*.c"
                "${root}/*.cc"
                "${root}/*.cxx"
                "${root}/*.cpp"
                "${root}/*.inl"
            )
            list(APPEND globbed_files ${root_files})
        endif()
    endforeach()

    set(filtered_files "")
    foreach(file_path IN LISTS globbed_files)
        file(TO_CMAKE_PATH "${file_path}" normalized_path)
        if(normalized_path MATCHES "/(build|_deps|third_party)/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/render/src/opengl/glad\\.c$")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/render/include/irreden/render/gl_wrap/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/script/include/lua54/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/render/src/metal/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/render/include/irreden/render/metal/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/render/third_party/metal-cpp/")
            continue()
        endif()
        if(normalized_path MATCHES "/engine/prefabs/irreden/render/systems/copilot_nonesense\\.cpp$")
            continue()
        endif()
        list(APPEND filtered_files "${normalized_path}")
    endforeach()

    list(REMOVE_DUPLICATES filtered_files)
    set(${out_var} "${filtered_files}" PARENT_SCOPE)
endfunction()

function(irreden_add_quality_targets)
    if(NOT IRREDEN_ENABLE_QUALITY_TOOLS)
        return()
    endif()

    irreden_collect_quality_files(irreden_quality_files)
    if(irreden_quality_files STREQUAL "")
        message(WARNING "No files found for quality targets.")
        return()
    endif()

    set(irreden_quality_file_list "${PROJECT_BINARY_DIR}/irreden_quality_files.cmake")
    file(WRITE "${irreden_quality_file_list}" "set(QUALITY_FILES\n")
    foreach(file_path IN LISTS irreden_quality_files)
        file(APPEND "${irreden_quality_file_list}" "    \"${file_path}\"\n")
    endforeach()
    file(APPEND "${irreden_quality_file_list}" ")\n")

    find_program(IRREDEN_CLANG_FORMAT_BIN NAMES clang-format)
    if(IRREDEN_CLANG_FORMAT_BIN)
        add_custom_target(format
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_FORMAT_BIN="${IRREDEN_CLANG_FORMAT_BIN}"
                -DQUALITY_FILE_LIST="${irreden_quality_file_list}"
                -DFORMAT_MODE=fix
                -P "${PROJECT_SOURCE_DIR}/cmake/run_clang_format.cmake"
            COMMENT "Formatting source files with clang-format"
            VERBATIM
        )

        add_custom_target(format-check
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_FORMAT_BIN="${IRREDEN_CLANG_FORMAT_BIN}"
                -DQUALITY_FILE_LIST="${irreden_quality_file_list}"
                -DFORMAT_MODE=check
                -P "${PROJECT_SOURCE_DIR}/cmake/run_clang_format.cmake"
            COMMENT "Checking source formatting with clang-format"
            VERBATIM
        )
    else()
        message(STATUS "clang-format not found; format targets will print install guidance.")
        add_custom_target(format
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red
                "clang-format not found. Install LLVM/clang tools and ensure clang-format is on PATH."
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )

        add_custom_target(format-check
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red
                "clang-format not found. Install LLVM/clang tools and ensure clang-format is on PATH."
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )
    endif()

    find_program(IRREDEN_CLANG_TIDY_BIN NAMES clang-tidy)
    if(IRREDEN_CLANG_TIDY_BIN)
        add_custom_target(lint
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_TIDY_BIN="${IRREDEN_CLANG_TIDY_BIN}"
                -DBUILD_DIR="${PROJECT_BINARY_DIR}"
                -DPROJECT_ROOT="${PROJECT_SOURCE_DIR}"
                -DQUALITY_FILE_LIST="${irreden_quality_file_list}"
                -P "${PROJECT_SOURCE_DIR}/cmake/run_clang_tidy.cmake"
            COMMENT "Running clang-tidy lint checks"
            VERBATIM
        )
    else()
        message(STATUS "clang-tidy not found; lint target will print install guidance.")
        add_custom_target(lint
            COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red
                "clang-tidy not found. Install LLVM/clang tools and ensure clang-tidy is on PATH."
            COMMAND ${CMAKE_COMMAND} -E false
            VERBATIM
        )
    endif()
endfunction()
