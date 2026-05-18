add_library(IrredenEngineScripting STATIC
    src/lua_script.cpp
    src/prefab_api.cpp
    src/lua_bindings_prefab.cpp
)

add_subdirectory(third_party/luajit)

target_compile_options(
    IrredenEngineProfile
    PUBLIC
    -O3
)

# Use sol2 library for C++ binding. Sol2's main branch supports both
# Lua 5.x and LuaJIT 2.x; it detects LuaJIT via LUAJIT_VERSION in
# lua.h and adapts its bindings automatically.
FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG main
)
FetchContent_MakeAvailable(sol2)
target_link_libraries(
    IrredenEngineScripting PUBLIC
    lua
    sol2
    IrredenEngineEntity
    IrredenEngineSystem
    IrredenEngineAsset
    # Required: component_voxel_set.hpp still pulls in <irreden/ir_render.hpp>
    # for IRRender::allocateVoxels / deallocateVoxels and active_canvas.hpp
    # for IRRender::getActiveCanvasEntityOrNull. ShapeType + SHAPE_FLAG_* are
    # already render-neutral (IRMath::SDF) so they no longer drag IRRender in.
    # See #739 for the layering fix; T-205 (#772) split active_canvas.hpp;
    # T-206 (re-landing) will split the voxel pool API.
    IrredenEngineRendering
    # T-193 PR 2: lua_command_bindings.hpp exposes IRCommand on the Lua
    # surface, so the public scripting interface needs the IRCommand
    # umbrella in scope wherever LuaScript is included.
    IrredenEngineCommand
)
# PRIVATE so render headers don't leak through script's PUBLIC interface
# (T-188 layering rule). component_voxel_set.hpp drags IRRender symbols
# in transitively until the T-206 voxel-pool API split re-lands on master.
target_link_libraries(
    IrredenEngineScripting PRIVATE
    IrredenEngineRendering
)
# Force sol2 to use its try/catch trampoline rather than letting C++
# exceptions propagate through the LuaJIT VM. With LuaJIT 2.1 sol2's
# default is exception propagation; in practice the exception message
# is lost between the LuaJIT unwind path and pcall's error capture
# (the caller observes the generic "C++ exception" string instead of
# what() text). Forcing SOL_EXCEPTIONS_ALWAYS_UNSAFE means sol2 catches
# the std::exception in-trampoline, extracts what(), and forwards via
# lua_error — preserving the specific message in protected_function
# results. The "unsafe" name is a sol2 convention referring to "no
# native unwind"; it is the safer choice for our tooling.
target_compile_definitions(
    IrredenEngineScripting PUBLIC
    SOL_EXCEPTIONS_ALWAYS_UNSAFE=1
)
target_include_directories(
    IrredenEngineScripting
    PUBLIC
    ${sol2_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
