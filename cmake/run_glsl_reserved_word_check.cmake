cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PROJECT_ROOT OR PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()
string(REPLACE "\"" "" PROJECT_ROOT "${PROJECT_ROOT}")
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" ABSOLUTE)

set(PROJECT_SOURCE_DIR "${PROJECT_ROOT}")
include("${CMAKE_CURRENT_LIST_DIR}/ir_quality_tools.cmake")

irreden_collect_shader_files(shader_files)
list(LENGTH shader_files shader_count)
if(shader_count EQUAL 0)
    message(FATAL_ERROR
        "GLSL reserved-word check collected 0 shader file(s) under engine, "
        "creations, test, and tools -- the glob is mis-scoped. Fix the path "
        "rather than trusting a clean result with nothing scanned."
    )
endif()

# GLSL 4.60 section 3.6 reserves these words for future use. packed is also
# rejected as an identifier by NVIDIA's GLSL front end.
set(glsl_reserved_words
    common partition active asm class union enum typedef template this
    resource goto inline noinline public static extern external interface
    long short half fixed unsigned superp input output hvec2 hvec3 hvec4
    fvec2 fvec3 fvec4 sampler3DRect filter sizeof cast namespace using packed
)
list(JOIN glsl_reserved_words "|" reserved_word_pattern)
set(declaration_pattern
    "(^|[^A-Za-z0-9_.])[A-Za-z_][A-Za-z0-9_]*[ \t]+(${reserved_word_pattern})[ \t]*([;=,)(\\[{]|$)"
)

set(violations "")
foreach(shader_file IN LISTS shader_files)
    file(READ "${shader_file}" shader_source)
    set(line_number 0)
    set(in_block_comment FALSE)
    while(NOT shader_source STREQUAL "")
        string(FIND "${shader_source}" "\n" newline_index)
        if(newline_index EQUAL -1)
            set(line "${shader_source}")
            set(shader_source "")
        else()
            string(SUBSTRING "${shader_source}" 0 ${newline_index} line)
            math(EXPR after_newline "${newline_index} + 1")
            string(SUBSTRING "${shader_source}" ${after_newline} -1 shader_source)
        endif()
        math(EXPR line_number "${line_number} + 1")

        set(uncommented_line "")
        set(line_remainder "${line}")
        while(NOT line_remainder STREQUAL "")
            if(in_block_comment)
                string(FIND "${line_remainder}" "*/" block_end)
                if(block_end EQUAL -1)
                    set(line_remainder "")
                    continue()
                endif()
                math(EXPR after_block_end "${block_end} + 2")
                string(SUBSTRING "${line_remainder}" ${after_block_end} -1 line_remainder)
                set(in_block_comment FALSE)
                continue()
            endif()

            string(FIND "${line_remainder}" "//" line_comment_start)
            string(FIND "${line_remainder}" "/*" block_start)
            if(line_comment_start GREATER_EQUAL 0 AND
                    (block_start EQUAL -1 OR line_comment_start LESS block_start))
                if(line_comment_start GREATER 0)
                    string(SUBSTRING "${line_remainder}" 0 ${line_comment_start} prefix)
                    string(APPEND uncommented_line "${prefix}")
                endif()
                set(line_remainder "")
            elseif(block_start GREATER_EQUAL 0)
                if(block_start GREATER 0)
                    string(SUBSTRING "${line_remainder}" 0 ${block_start} prefix)
                    string(APPEND uncommented_line "${prefix}")
                endif()
                math(EXPR after_block_start "${block_start} + 2")
                string(SUBSTRING "${line_remainder}" ${after_block_start} -1 line_remainder)
                set(in_block_comment TRUE)
            else()
                string(APPEND uncommented_line "${line_remainder}")
                set(line_remainder "")
            endif()
        endwhile()
        set(line "${uncommented_line}")

        if(line MATCHES "^[ \t]*#")
            continue()
        endif()
        if(NOT line MATCHES "${declaration_pattern}")
            continue()
        endif()

        set(reserved_word "${CMAKE_MATCH_2}")
        string(STRIP "${line}" source_line)
        file(RELATIVE_PATH relative_shader "${PROJECT_ROOT}" "${shader_file}")
        get_filename_component(shader_dir "${shader_file}" DIRECTORY)
        get_filename_component(shader_stem "${shader_file}" NAME_WE)
        set(report_line
            "${relative_shader}:${line_number}: reserved GLSL word '${reserved_word}' used as an identifier: ${source_line}"
        )

        set(metal_twin "${shader_dir}/metal/${shader_stem}.metal")
        if(EXISTS "${metal_twin}")
            file(RELATIVE_PATH relative_twin "${PROJECT_ROOT}" "${metal_twin}")
            string(APPEND report_line
                "\n  Rename the identifier in ${relative_twin} too so the GLSL/Metal pair stays textually parallel."
            )
        endif()
        string(REPLACE ";" "\\;" report_line "${report_line}")
        list(APPEND violations "${report_line}")
    endwhile()
endforeach()

message(STATUS "GLSL reserved-word check scanned ${shader_count} shader file(s).")

if(violations)
    list(JOIN violations "\n" violations_joined)
    message(FATAL_ERROR
        "GLSL reserved words may not be used as declared identifiers. Choose "
        "a descriptive replacement; this check does not auto-fix names.\n"
        "${violations_joined}"
    )
endif()
