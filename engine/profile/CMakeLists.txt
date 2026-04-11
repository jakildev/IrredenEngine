add_library(IrredenEngineProfile STATIC
    src/cpu_profiler.cpp
    src/logger_spd.cpp
    src/ir_profile.cpp
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

if(IR_isDarwin)
    foreach(_irreden_qt5_root IN ITEMS /opt/homebrew/opt/qt@5 /usr/local/opt/qt@5)
        if(EXISTS "${_irreden_qt5_root}")
            list(APPEND CMAKE_PREFIX_PATH "${_irreden_qt5_root}")
        endif()
    endforeach()
endif()
if(IR_isLinux)
    # Typical distro Qt5 locations. find_package(Qt5Widgets) inside
    # easy_profiler will pick the first one that exists.
    foreach(_irreden_qt5_root IN ITEMS
        /usr/lib/x86_64-linux-gnu/qt5
        /usr/lib/qt5
        /usr/lib64/qt5
        /usr
    )
        if(EXISTS "${_irreden_qt5_root}")
            list(APPEND CMAKE_PREFIX_PATH "${_irreden_qt5_root}")
        endif()
    endforeach()
endif()

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