cmake_minimum_required(VERSION 3.15)

project(enkiTS LANGUAGES CXX)

# enkiTS — Doug Binks' task scheduler. zlib license, header + a handful
# of .cpp files. Upstream is a vanilla CMake project so
# FetchContent_MakeAvailable is enough; no native Makefile invocation
# (the LuaJIT precedent next door) needed.
#
# Default fetches the v1.11 commit as a hash-checked source archive (no git
# history lands in _deps). Set IR_ENKITS_GIT_REF to a branch or tag name to
# shallow-clone a mutable reference instead.
# The ref is the only knob: enkiTS' own CMakeLists.txt builds a static
# library called `enkiTS` by default, which we re-export as the IMPORTED
# target `enkits` to keep the engine-side link name consistent with the
# `lua` rename in `engine/script/third_party/luajit/CMakeLists.txt`.
include(FetchContent)

set(IR_ENKITS_GIT_REF "" CACHE STRING
    "enkiTS branch or tag to shallow-clone instead of the pinned v1.11 archive")

# Force-disable enkiTS' optional targets so the vendor add_subdirectory
# does not pollute our `all` target with example binaries or its test
# executable. These cache entries must be set BEFORE FetchContent runs
# enkiTS' CMakeLists.txt.
include("${CMAKE_CURRENT_LIST_DIR}/isolate_completion_sources.cmake")

set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
set(ENKITS_INSTALL OFF CACHE BOOL "" FORCE)

if(IR_ENKITS_GIT_REF)
    FetchContent_Declare(
        enkiTS_src
        GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
        GIT_TAG        ${IR_ENKITS_GIT_REF}
        GIT_SHALLOW    TRUE
    )
else()
    # v1.11 — 6ffccbdb1000253d8d513dd7a5ae9226e5023a5c
    FetchContent_Declare(
        enkiTS_src
        URL      https://github.com/dougbinks/enkiTS/archive/6ffccbdb1000253d8d513dd7a5ae9226e5023a5c.tar.gz
        URL_HASH SHA256=a56759d215aa4a9ea2cfe74f80ba92d15de5db2dbef5a268aba320d6b60a0ab0
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif()

FetchContent_MakeAvailable(enkiTS_src)
set(FETCHCONTENT_BASE_DIR "${_enkits_original_base}")

if(NOT IR_ENKITS_GIT_REF)
    set(ENKITS_SOURCE_DIR "${enkits_src_SOURCE_DIR}")
    include("${CMAKE_CURRENT_LIST_DIR}/apply_completion_ordering.cmake")
endif()

# Match the lua/luajit pattern: alias the upstream target name to a
# stable downstream name. enkiTS upstream produces a static lib called
# `enkiTS` (lowercase variants on some forks); we expose it as
# `enkits` so the engine link line stays canonical even if upstream
# renames.
if(TARGET enkiTS)
    # enkiTS v1.11's TaskScheduler.cpp still uses `std::is_pod`, which
    # was deprecated in C++17 and removed in C++23 (engine builds at
    # CXX_STANDARD 23). Force C++17 for the vendor target only — the
    # public header is C++14-clean and the engine link line stays
    # CXX_STANDARD 23. Drop this override once we pin to an enkiTS
    # revision that has migrated off `is_pod`.
    set_target_properties(enkiTS PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
    )
    # Upstream enkiTS uses `include_directories()` (global, not
    # target-scoped), so consumers reached via target_link_libraries
    # see no include path. Re-export the headers as an INTERFACE
    # include directory on the upstream target so the ALIAS below
    # propagates them through `target_link_libraries(IrredenEngineJob
    # PUBLIC enkits)`.
    target_include_directories(enkiTS INTERFACE
        ${enkits_src_SOURCE_DIR}/src
    )
    add_library(enkits ALIAS enkiTS)
else()
    message(FATAL_ERROR
        "enkiTS FetchContent did not produce an `enkiTS` target. "
        "Upstream may have renamed it; update engine/job/third_party/"
        "enkiTS/CMakeLists.txt to alias the new name.")
endif()
