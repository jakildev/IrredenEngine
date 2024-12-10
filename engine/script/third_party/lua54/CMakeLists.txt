cmake_minimum_required(VERSION 3.15)

project(lua LANGUAGES C)

# Collect all Lua source files
set(LUA_SOURCES
    src/lapi.c
    src/lauxlib.c
    src/lbaselib.c
    src/lcode.c
    src/lcorolib.c
    src/lctype.c
    src/ldblib.c
    src/ldebug.c
    src/ldo.c
    src/ldump.c
    src/lfunc.c
    src/lgc.c
    src/linit.c
    src/liolib.c
    src/llex.c
    src/lmathlib.c
    src/lmem.c
    src/loadlib.c
    src/lobject.c
    src/lopcodes.c
    src/loslib.c
    src/lparser.c
    src/lstate.c
    src/lstring.c
    src/lstrlib.c
    src/ltable.c
    src/ltablib.c
    src/ltm.c
    src/lua.c
    src/luac.c
    src/lundump.c
    src/lutf8lib.c
    src/lvm.c
    src/lzio.c
)

# Add Lua include directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Define the Lua library
add_library(lua STATIC ${LUA_SOURCES})
target_include_directories(lua PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Set platform-specific flags
if(WIN32)
    target_compile_definitions(lua PRIVATE LUA_BUILD_AS_DLL)
endif()

# Ensure the library compiles as C code
set_target_properties(lua PROPERTIES LINKER_LANGUAGE C)
