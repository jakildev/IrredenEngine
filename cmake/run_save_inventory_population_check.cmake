# Executed population check for the engine save-policy inventory (#2834).
#
# The compile-time gate can only inspect component types already named in
# AllEngineComponents. This check instead inventories component declarations in
# engine headers, so a never-listed type cannot remain invisible. Source files
# are excluded by construction because their private system-registration anchor
# types do not participate in world snapshots. Render backends are included;
# only vendored third-party headers are outside the census.
#
# Forward declarations count as live component names. There are none today; if
# one appears, its save policy must be decided explicitly rather than silently
# allowlisted here.

if(NOT DEFINED PROJECT_ROOT OR PROJECT_ROOT STREQUAL "")
    message(FATAL_ERROR "PROJECT_ROOT is required.")
endif()
string(REPLACE "\"" "" PROJECT_ROOT "${PROJECT_ROOT}")
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" ABSOLUTE)

set(inventory_header
    "${PROJECT_ROOT}/engine/world/include/irreden/world/save_component_inventory.hpp")
if(NOT EXISTS "${inventory_header}")
    message(FATAL_ERROR
        "Save inventory population check: ${inventory_header} not found. "
        "The inventory anchor is required; fix the path rather than trusting "
        "a clean result with no inventory parsed."
    )
endif()

function(ir_strip_cpp_comments input_text output_var)
    set(stripped "${input_text}")
    string(REGEX REPLACE "/\\*([^*]|\\*+[^*/])*\\*+/" "" stripped "${stripped}")
    string(REGEX REPLACE "//[^\n]*" "" stripped "${stripped}")
    set(${output_var} "${stripped}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE component_headers
    "${PROJECT_ROOT}/engine/*.h"
    "${PROJECT_ROOT}/engine/*.hpp"
)
list(FILTER component_headers EXCLUDE REGEX "/engine/render/third_party/")

set(live_components "")
foreach(header IN LISTS component_headers)
    file(READ "${header}" header_text)
    ir_strip_cpp_comments("${header_text}" header_text)
    string(REGEX MATCHALL
        "(struct|class)[ \\t\\r\\n]+C_[A-Za-z0-9_]+"
        declarations "${header_text}")
    foreach(declaration IN LISTS declarations)
        string(REGEX MATCH "C_[A-Za-z0-9_]+" component "${declaration}")
        list(APPEND live_components "${component}")
        file(RELATIVE_PATH relative_header "${PROJECT_ROOT}" "${header}")
        set("declaring_header_${component}" "${relative_header}")
    endforeach()
endforeach()
list(REMOVE_DUPLICATES live_components)
list(SORT live_components)

list(LENGTH live_components live_count)
if(live_count EQUAL 0)
    message(FATAL_ERROR
        "Save inventory population check collected 0 component declarations "
        "from ${PROJECT_ROOT}/engine -- the glob or declaration matcher is "
        "mis-scoped. Fix it rather than trusting a clean result."
    )
endif()

file(READ "${inventory_header}" inventory_text)
ir_strip_cpp_comments("${inventory_text}" inventory_text)

string(REGEX MATCHALL
    "IR_SAVE_OPT_(IN|OUT)\\([^)]*(IRComponents|IRSystem)::C_[A-Za-z0-9_]+"
    decision_matches "${inventory_text}")
set(decision_components "")
foreach(decision IN LISTS decision_matches)
    string(REGEX MATCH "C_[A-Za-z0-9_]+" component "${decision}")
    list(APPEND decision_components "${component}")
endforeach()
list(REMOVE_DUPLICATES decision_components)

string(REGEX MATCH
    "using[ \\t\\r\\n]+AllEngineComponents[ \\t\\r\\n]*=[ \\t\\r\\n]*std::tuple<[^;]*>;"
    tuple_block "${inventory_text}")
if(tuple_block STREQUAL "")
    message(FATAL_ERROR
        "Save inventory population check could not parse AllEngineComponents "
        "in ${inventory_header}; update the checker anchor rather than "
        "letting the scan pass without the inventory tuple."
    )
endif()

string(REGEX MATCHALL
    "(IRComponents|IRSystem)::C_[A-Za-z0-9_]+"
    tuple_matches "${tuple_block}")
set(tuple_components "")
foreach(tuple_entry IN LISTS tuple_matches)
    string(REGEX MATCH "C_[A-Za-z0-9_]+" component "${tuple_entry}")
    list(APPEND tuple_components "${component}")
endforeach()
list(REMOVE_DUPLICATES tuple_components)

list(LENGTH decision_components decision_count)
list(LENGTH tuple_components tuple_count)
if(decision_count EQUAL 0 OR tuple_count EQUAL 0)
    message(FATAL_ERROR
        "Save inventory population check parsed an empty decision or tuple "
        "set from ${inventory_header}; update the checker anchors rather than "
        "trusting a clean result."
    )
endif()

set(failures "")
foreach(component IN LISTS live_components)
    list(FIND decision_components "${component}" decision_index)
    if(decision_index EQUAL -1)
        string(APPEND failures
            "\n${component}: missing decision"
            "\n  declared in: ${declaring_header_${component}}"
            "\n  remedy: add IR_SAVE_OPT_IN/OPT_OUT + AllEngineComponents "
            "entry in save_component_inventory.hpp.")
    endif()

    list(FIND tuple_components "${component}" tuple_index)
    if(tuple_index EQUAL -1)
        string(APPEND failures
            "\n${component}: missing tuple entry"
            "\n  declared in: ${declaring_header_${component}}"
            "\n  remedy: add IR_SAVE_OPT_IN/OPT_OUT + AllEngineComponents "
            "entry in save_component_inventory.hpp.")
    endif()
endforeach()

message(STATUS
    "Save inventory population check scanned ${live_count} component name(s) "
    "against ${decision_count} decision(s) and ${tuple_count} tuple entry name(s)."
)

if(NOT failures STREQUAL "")
    message(FATAL_ERROR "Save inventory population check failed:${failures}")
endif()
