add_library(IrredenEngineProfiling STATIC
    cpu_profiler.cpp
    debug_helper.cpp
    logger_spd.cpp
    logger_spd.hpp
)

target_compile_options(
    IrredenEngineProfiling
    PUBLIC
    -O3
)

# Remove to disable profiling
target_compile_definitions(IrredenEngineProfiling PUBLIC BUILD_WITH_EASY_PROFILER)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)
target_link_libraries(IrredenEngineProfiling PUBLIC spdlog)
target_include_directories(IrredenEngineProfiling PUBLIC
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
FetchContent_Declare(
    easy_profiler
    GIT_REPOSITORY https://github.com/yse/easy_profiler
    GIT_TAG v2.1.0
)
FetchContent_MakeAvailable(easy_profiler)
target_link_libraries(IrredenEngineProfiling PUBLIC easy_profiler)
target_include_directories(IrredenEngineProfiling PUBLIC
    ${easy_profiler_SOURCE_DIR}/easy_profiler_core/include
)

add_custom_command(
    TARGET IrredenEngineProfiling
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        ${easy_profiler_BINARY_DIR}/bin/libeasy_profilerd.dll
        ${PROJECT_BINARY_DIR}/libeasy_profilerd.dll
)

