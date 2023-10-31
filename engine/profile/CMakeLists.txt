add_library(IrredenEngineProfiling STATIC
    src/cpu_profiler.cpp
    src/logger_spd.cpp
    src/ir_profile.cpp
    # src/logger_spd.hpp
)

target_compile_options(
    IrredenEngineProfiling
    PUBLIC
    -O3
)

# fmt (dependency of spdlog)
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG master
)
FetchContent_MakeAvailable(fmt)
add_custom_command(
    TARGET IrredenEngineProfiling
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${fmt_BINARY_DIR}/bin/libfmtd.dll
        ${PROJECT_BINARY_DIR}/libfmtd.dll
)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG v1.12.0
)
option(
    IR_SPDLOG_FMT_EXTERNAL
    "Use external fmt library instead of bundled"
    ON
)
FetchContent_GetProperties(spdlog)
if(NOT spdlog_POPULATED)
    FetchContent_Populate(spdlog)
    set(SPDLOG_FMT_EXTERNAL ${IR_SPDLOG_FMT_EXTERNAL})
endif()
add_subdirectory(
    ${spdlog_SOURCE_DIR}
    ${spdlog_BINARY_DIR}
)

target_link_libraries(IrredenEngineProfiling PUBLIC spdlog)
target_include_directories(IrredenEngineProfiling PRIVATE
    ${spdlog_SOURCE_DIR}/include
)

add_custom_command(
    TARGET IrredenEngineProfiling
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
    ${spdlog_BINARY_DIR}/libspdlogd.dll
    ${PROJECT_BINARY_DIR}/libspdlogd.dll
)

# easy_profiler

# Remove to disable profiling
target_compile_definitions(
    IrredenEngineProfiling
    PUBLIC
    BUILD_WITH_EASY_PROFILER
)

FetchContent_Declare(
    easy_profiler
    GIT_REPOSITORY https://github.com/yse/easy_profiler
    GIT_TAG v2.1.0
)
FetchContent_MakeAvailable(easy_profiler)
target_link_libraries(IrredenEngineProfiling PUBLIC easy_profiler)
target_include_directories(
    IrredenEngineProfiling
    PRIVATE
    ${easy_profiler_SOURCE_DIR}/easy_profiler_core/include
)

add_custom_command(
    TARGET IrredenEngineProfiling
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${easy_profiler_BINARY_DIR}/bin/libeasy_profilerd.dll
        ${PROJECT_BINARY_DIR}/libeasy_profilerd.dll
)

target_include_directories(
    IrredenEngineProfiling
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)