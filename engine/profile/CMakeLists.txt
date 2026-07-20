add_library(IrredenEngineProfile STATIC
    src/cpu_profiler.cpp
    src/logger_spd.cpp
    src/ir_profile.cpp
    src/profile_report.cpp
)

option(
    IRREDEN_PROFILE_ENABLE_EASY_PROFILER_GUI
    "Build easy_profiler profiler_gui when Qt5Widgets is available."
    ON
)

target_compile_options(
    IrredenEngineProfile
    PUBLIC
    -O3
)

# fmt (dependency of spdlog). Pinned to 11.2.0: fmt <=10.2.1 uses a consteval
# FMT_STRING form that Apple clang 21 (clang-2100.x) rejects at C++23, breaking
# every fresh macOS build in the fmt lib itself (#2449). 11.2.0 compiles clean
# under Apple clang 21 and stays fine on gcc-13 / mingw. Paired with spdlog
# v1.15.3 below — spdlog 1.12 cannot compile against external fmt 11.
FetchContent_Declare(fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 11.2.0
)
FetchContent_MakeAvailable(fmt)

IR_copyDLL(
    IrredenEngineProfile
    libfmtd
    ${fmt_BINARY_DIR}/bin
)

# spdlog. Bumped to v1.15.3 as a required pair with fmt 11.2.0 above: spdlog
# 1.12 expects the fmt-10-era basic_format_string and fails to compile against
# external fmt 11 (#2449). 1.15.3 compiles clean with SPDLOG_FMT_EXTERNAL + fmt
# 11.2.0; the SPDLOG_FMT_EXTERNAL wiring and the libspdlogd artifact name are
# unchanged from 1.12.
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog
    GIT_TAG v1.15.3
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

if(IR_isDarwin)
    foreach(_irreden_qt5_root IN ITEMS /opt/homebrew/opt/qt@5 /usr/local/opt/qt@5)
        if(EXISTS "${_irreden_qt5_root}")
            list(APPEND CMAKE_PREFIX_PATH "${_irreden_qt5_root}")
        endif()
    endforeach()
endif()
# Linux: apt-installed qtbase5-dev puts Qt5Config.cmake under /usr/lib/<arch>/cmake/Qt5,
# which find_package already searches via CMake's system prefix probing, so no explicit
# hint list is needed.

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
set(EASY_PROFILER_NO_GUI ON CACHE BOOL "" FORCE)
if(IRREDEN_PROFILE_ENABLE_EASY_PROFILER_GUI)
    set(EASY_PROFILER_NO_GUI OFF CACHE BOOL "" FORCE)
endif()
# We link only the easy_profiler core library. Its bundled sample/reader
# executables are dead weight in our build and their link step adds a stale
# `${CMAKE_SOURCE_DIR}/../bin` search path, which the macOS linker warns about
# ("search path '…/../bin' not found"). Skip building them.
set(EASY_PROFILER_NO_SAMPLES ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(easy_profiler)
target_link_libraries(IrredenEngineProfile PUBLIC easy_profiler)
target_include_directories(
    IrredenEngineProfile
    PRIVATE
    ${easy_profiler_SOURCE_DIR}/easy_profiler_core/include
)

IR_copyDLL(
    IrredenEngineProfile
    libeasy_profilerd
    ${easy_profiler_BINARY_DIR}/bin
)

if(IRREDEN_PROFILE_ENABLE_EASY_PROFILER_GUI)
    if(TARGET profiler_gui)
        message(STATUS "IrredenEngineProfile: easy_profiler GUI enabled.")
    else()
        message(
            WARNING
            "IrredenEngineProfile: profiler_gui requested but Qt5Widgets was not found. "
            "Install qt@5 and ensure CMAKE_PREFIX_PATH includes its prefix."
        )
    endif()
else()
    message(STATUS "IrredenEngineProfile: easy_profiler GUI disabled.")
endif()

target_include_directories(
    IrredenEngineProfile
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
