if(NOT DEFINED QUALITY_FILE_LIST OR QUALITY_FILE_LIST STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILE_LIST is required.")
endif()
string(REPLACE "\"" "" QUALITY_FILE_LIST "${QUALITY_FILE_LIST}")

include("${QUALITY_FILE_LIST}")
if(NOT DEFINED QUALITY_FILES OR QUALITY_FILES STREQUAL "")
    message(FATAL_ERROR "QUALITY_FILES is empty. No files to check.")
endif()

set(anonymous_namespace_failures "")
set(feature_detail_namespace_failures "")
set(file_count 0)

foreach(file_path IN LISTS QUALITY_FILES)
    file(TO_CMAKE_PATH "${file_path}" normalized_file_path)
    if(normalized_file_path MATCHES "/(build|_deps|third_party)/")
        continue()
    endif()
    if(NOT normalized_file_path MATCHES "\\.(h|hpp|inl)$")
        continue()
    endif()

    math(EXPR file_count "${file_count} + 1")
    file(READ "${normalized_file_path}" file_contents)

    string(REGEX MATCH "(^|[\r\n])[ \t]*namespace[ \t\r\n]*\\{" has_anonymous_namespace "${file_contents}")
    if(has_anonymous_namespace)
        list(APPEND anonymous_namespace_failures "${normalized_file_path}")
    endif()

    string(REGEX MATCH "namespace[ \t\r\n]+[A-Za-z_][A-Za-z0-9_]*Detail[ \t\r\n]*\\{" has_feature_detail_namespace "${file_contents}")
    if(has_feature_detail_namespace)
        list(APPEND feature_detail_namespace_failures "${normalized_file_path}")
    endif()
endforeach()

message(STATUS "Header convention checks scanned ${file_count} header file(s).")

if(anonymous_namespace_failures)
    list(JOIN anonymous_namespace_failures "\n  - " anonymous_namespace_failures_joined)
    message(FATAL_ERROR
        "Anonymous namespaces are not allowed in headers. Use a nested `detail` namespace "
        "or move the implementation to a .cpp file.\n"
        "  - ${anonymous_namespace_failures_joined}"
    )
endif()

if(feature_detail_namespace_failures)
    list(JOIN feature_detail_namespace_failures "\n  - " feature_detail_namespace_failures_joined)
    message(FATAL_ERROR
        "Feature-specific `*Detail` namespaces are not allowed in headers. Prefer the "
        "nested lowercase `detail` convention unless the helper namespace is an intentional "
        "shared submodule.\n"
        "  - ${feature_detail_namespace_failures_joined}"
    )
endif()
