add_library(IrredenEngineProfile STATIC
    src/cpu_profiler.cpp
    src/logger_spd.cpp
    src/ir_profile.cpp
)

target_compile_options(
    IrredenEngineProfile
    PUBLIC
    -O3
)

# fmt (dependency of spdlog)
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 10.1.1
)
FetchContent_MakeAvailable(fmt)

IR_copyDLL(
    IrredenEngineProfile
    libfmtd
    ${fmt_BINARY_DIR}/bin
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

target_link_libraries(IrredenEngineProfile PUBLIC spdlog)
target_include_directories(IrredenEngineProfile PRIVATE
    ${spdlog_SOURCE_DIR}/include
)

IR_copyDLL(
    IrredenEngineProfile
    libspdlogd
    ${spdlog_BINARY_DIR}
)
# easy_profiler

# Remove to disable profiling
target_compile_definitions(
    IrredenEngineProfile
    PUBLIC
    BUILD_WITH_EASY_PROFILER
)

FetchContent_Declare(
    easy_profiler
    GIT_REPOSITORY https://github.com/yse/easy_profiler
    GIT_TAG v2.1.0
)
FetchContent_MakeAvailable(easy_profiler)
target_link_libraries(IrredenEngineProfile PUBLIC easy_profiler)
target_include_directories(
    IrredenEngineProfile
    PRIVATE
    ${easy_profiler_SOURCE_DIR}/easy_profiler_core/include
)

IR_copyDLL(
    IrredenEngineProfile
    libeasy_profilerd.dll
    ${easy_profiler_BINARY_DIR}/bin
)

target_include_directories(
    IrredenEngineProfile
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)