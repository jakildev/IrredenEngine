cmake_minimum_required(VERSION 3.15)

project(luajit LANGUAGES C)

# LuaJIT 2.1 rolling branch — a fresh configure fetches the current
# v2.1 tip. Override IR_LUAJIT_GIT_TAG to a commit SHA for a
# reproducible build. Sol2 detects LuaJIT via LUAJIT_VERSION in lua.h
# and adapts its bindings accordingly, so the C++ surface in
# engine/script/ does not change with this swap.
#
# Vendoring strategy: FetchContent populates the upstream source tree;
# a custom build target invokes LuaJIT's native Makefile so the
# DynASM-preprocessed VM assembly is produced by the toolchain that
# upstream tests with. Replicating that in pure CMake is fragile
# (DynASM emits per-arch .S from .dasc inputs through a host-built
# `buildvm`).
include(FetchContent)

set(IR_LUAJIT_GIT_TAG "v2.1" CACHE STRING
    "LuaJIT git ref (branch, tag, or commit) to fetch and build")

FetchContent_Declare(
    luajit_src
    GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
    GIT_TAG        ${IR_LUAJIT_GIT_TAG}
    GIT_SHALLOW    TRUE
)

# LuaJIT does not ship a root CMakeLists.txt, so FetchContent_MakeAvailable
# populates the source without invoking add_subdirectory.
FetchContent_MakeAvailable(luajit_src)

set(LUAJIT_SRC_DIR ${luajit_src_SOURCE_DIR})
set(LUAJIT_INCLUDE_DIR ${LUAJIT_SRC_DIR}/src)

# Output static-library name produced by LuaJIT's Makefile. The engine
# builds LuaJIT through its upstream *Unix* Makefile on every supported
# toolchain — including Windows, where the `windows-debug` preset uses
# MinGW Makefiles + MSYS2 `make` (not MSVC) — so the archive is always
# `libluajit.a`. A prior `if(WIN32)` branch expected a versioned
# `libluajit-5.1.a` that the MinGW build never emits, which silently broke
# the native-Windows link (latent because the fleet only built on WSL /
# macOS). MSVC's msvcbuild.bat would emit `lua51.lib`, but the engine has
# no MSVC preset.
set(LUAJIT_LIB_NAME libluajit.a)
set(LUAJIT_LIB_PATH ${LUAJIT_INCLUDE_DIR}/${LUAJIT_LIB_NAME})

# `make BUILDMODE=static` forces the static archive even when the
# upstream Makefile defaults to dynamic on a host. Keeps the IMPORTED
# target's location stable.
#
# `XCFLAGS=-DLUAJIT_UNWIND_EXTERNAL` enables LuaJIT's external-unwind
# path so C++ exceptions thrown from sol2-bound functions propagate
# through the LuaJIT VM. Without this, sol2 errors arrive at the Lua
# protected_function as a generic "C++ exception" string with the
# original message lost. The flag is documented in LuaJIT's INSTALL
# (Building with C++ Exceptions section).
set(LUAJIT_MAKE_ARGS BUILDMODE=static XCFLAGS=-DLUAJIT_UNWIND_EXTERNAL)

if(APPLE)
    # LuaJIT requires MACOSX_DEPLOYMENT_TARGET to be set before invoking
    # its Makefile (see upstream INSTALL); pick the same floor the rest
    # of the engine builds against.
    list(APPEND LUAJIT_MAKE_ARGS MACOSX_DEPLOYMENT_TARGET=11.0)
endif()

# CMake's IMPORTED-library validation requires the include directory to
# exist at configure time. FetchContent_Populate already created it, but
# we MAKE_DIRECTORY defensively in case a future cache-clean reconfigures
# before the populate step runs.
file(MAKE_DIRECTORY ${LUAJIT_INCLUDE_DIR})

# LuaJIT builds through its own Makefile, so this must invoke a *make*
# program — and on native Windows that program is `mingw32-make`, not
# `make`. The windows-debug preset uses MinGW Makefiles + MSYS2's mingw64
# toolchain, which ships `mingw32-make.exe`; the Unix `make` lives in
# MSYS2's /usr/bin, which is NOT on the build PATH (ir-build prepends only
# mingw64/bin). A bare `make` there dies with "no such file or directory"
# and breaks the whole native-Windows link — latent until the fleet first
# built on real Windows, because WSL/macOS both have `make`.
#
# Deliberately NOT ${CMAKE_MAKE_PROGRAM}: under a Ninja generator that is
# `ninja`, and `ninja BUILDMODE=static …` is nonsense — LuaJIT always
# needs a make. Select by platform instead.
if(WIN32)
    set(LUAJIT_MAKE_PROGRAM mingw32-make)
else()
    set(LUAJIT_MAKE_PROGRAM make)
endif()

add_custom_command(
    OUTPUT ${LUAJIT_LIB_PATH}
    WORKING_DIRECTORY ${LUAJIT_SRC_DIR}
    COMMAND ${CMAKE_COMMAND} -E env ${LUAJIT_MAKE_PROGRAM} ${LUAJIT_MAKE_ARGS}
    COMMENT "Building LuaJIT 2.1 static library (${LUAJIT_LIB_NAME})"
    VERBATIM
)

add_custom_target(luajit_build DEPENDS ${LUAJIT_LIB_PATH})

# Match the lua54 target name (`lua`) so dependent CMake code does not
# need to change.
add_library(lua STATIC IMPORTED GLOBAL)
add_dependencies(lua luajit_build)
set_target_properties(lua PROPERTIES
    IMPORTED_LOCATION ${LUAJIT_LIB_PATH}
    INTERFACE_INCLUDE_DIRECTORIES ${LUAJIT_INCLUDE_DIR}
)

# Platform link requirements documented in LuaJIT's INSTALL file.
if(UNIX AND NOT APPLE)
    target_link_libraries(lua INTERFACE ${CMAKE_DL_LIBS} m)
endif()

if(APPLE AND CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    # x86_64 macOS only: keep the LuaJIT VM image at low addresses so
    # its 32-bit GC pointers fit. Upstream INSTALL documents these flags
    # for 64-bit Intel macOS specifically. On Apple Silicon (arm64) the
    # GC uses a different layout and these flags produce a malformed
    # Mach-O when combined with PIE — the `-image_base` is ignored by
    # the modern linker but `-pagezero_size 10000` plus PIE breaks the
    # binary at load time. SHELL: prefix tells CMake to forward the
    # flag + value as two distinct argv entries to clang.
    set_property(TARGET lua APPEND PROPERTY INTERFACE_LINK_OPTIONS
        "SHELL:-pagezero_size 10000"
        "SHELL:-image_base 100000000"
    )
endif()

