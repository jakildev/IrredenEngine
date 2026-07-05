add_library(IrredenEngineScripting STATIC
    src/lua_script.cpp
    src/prefab_api.cpp
    src/prefab_component_factory.cpp
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
    # T-193 PR 2: lua_command_bindings.hpp exposes IRCommand on the Lua
    # surface, so the public scripting interface needs the IRCommand
    # umbrella in scope wherever LuaScript is included.
    IrredenEngineCommand
)
# Prefab headers (component_shape_descriptor, voxel_pool_api) transitively
# include lightweight render headers (active_canvas.hpp, lod_level.hpp,
# ir_render.hpp). The include path is pulled via generator expression so
# it tracks IrredenEngineRendering's INTERFACE_INCLUDE_DIRECTORIES
# automatically — no full library link; symbols resolve at final executable
# link time through IrredenEngineWorld / IrredenEngine.
# See #739 (T-201) and T-207 for the layering plan.
target_include_directories(
    IrredenEngineScripting PRIVATE
    $<TARGET_PROPERTY:IrredenEngineRendering,INTERFACE_INCLUDE_DIRECTORIES>
)
# lua_audio_bindings.hpp includes <irreden/ir_audio.hpp> (the IRAudio file-
# playback surface, #1813). Same generator-expression include-only pattern as
# the render glue above — IRAudio:: symbols resolve at final exe link time
# through IrredenEngineWorld, no full library link here.
target_include_directories(
    IrredenEngineScripting PRIVATE
    $<TARGET_PROPERTY:IrredenEngineAudio,INTERFACE_INCLUDE_DIRECTORIES>
)
# lua_world_snapshot_bindings.hpp includes <irreden/world/world_snapshot.hpp>
# (IRPersist world save/load, persist P7 #2218). Same include-only generator-
# expression pattern — IRWorld:: symbols resolve at final exe link through
# IrredenEngineWorld. NO library link: World links Scripting, so a link back
# would form a cycle.
target_include_directories(
    IrredenEngineScripting PRIVATE
    $<TARGET_PROPERTY:IrredenEngineWorld,INTERFACE_INCLUDE_DIRECTORIES>
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
