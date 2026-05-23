cmake_minimum_required(VERSION 3.15)

project(enkiTS LANGUAGES CXX)

# enkiTS — Doug Binks' task scheduler. zlib license, header + a handful
# of .cpp files. Upstream is a vanilla CMake project so
# FetchContent_MakeAvailable is enough; no native Makefile invocation
# (the LuaJIT precedent next door) needed.
#
# Override IR_ENKITS_GIT_TAG to a commit SHA for a reproducible build.
# The tag is the only knob: enkiTS' own CMakeLists.txt builds a static
# library called `enkiTS` by default, which we re-export as the IMPORTED
# target `enkits` to keep the engine-side link name consistent with the
# `lua` rename in `engine/script/third_party/luajit/CMakeLists.txt`.
include(FetchContent)

set(IR_ENKITS_GIT_TAG "v1.11" CACHE STRING
    "enkiTS git ref (branch, tag, or commit) to fetch and build")

# Force-disable enkiTS' optional targets so the vendor add_subdirectory
# does not pollute our `all` target with example binaries or its test
# executable. These cache entries must be set BEFORE FetchContent runs
# enkiTS' CMakeLists.txt.
set(ENKITS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_SHARED   OFF CACHE BOOL "" FORCE)
set(ENKITS_BUILD_C_INTERFACE OFF CACHE BOOL "" FORCE)
set(ENKITS_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    enkiTS_src
    GIT_REPOSITORY https://github.com/dougbinks/enkiTS.git
    GIT_TAG        ${IR_ENKITS_GIT_TAG}
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(enkiTS_src)

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
