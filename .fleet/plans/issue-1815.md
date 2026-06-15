# Plan: Packaging target — one-command self-contained per-platform game bundle

- **Issue:** #1815
- **Model:** sonnet  (heavy cross-platform judgment is front-loaded into this plan; execution is spec-following + a mechanical demo sweep)
- **Date:** 2026-06-14

## Scope

Deliver the **engine-side mechanism** for producing a self-contained,
per-platform bundle (`exe + data/ + shaders/ + scripts/ + runtime libs`)
that runs by double-click on a clean machine. Two reusable CMake helpers
plus a reference wiring on a public demo (`IRShapeDebug`) the fleet can
verify on Linux, plus docs.

The **game's own** bundle (`ir_package_creation(IRGame)`) is a *game-repo
follow-up* — the engine repo is public and cannot wire the gitignored game
target. This task ships the capability; the game consumes it later.

## Key facts (verified against current code)

- Runtime is already **flat + exe-relative**: `engine/include/irreden/ir_engine.hpp:59-70`
  `init()` does `std::filesystem::current_path(exeDir)`, then resolves
  `scripts/`, `data/...`, `shaders/...` from CWD. So the bundle layout ==
  the current per-demo build-dir layout. No rpath gymnastics needed for
  *assets*.
- **No CPack, no `install()` rules** exist anywhere today. Asset delivery
  is hand-rolled `add_custom_command(... copy_directory ...)`, duplicated
  verbatim across 23 demos (canonical: `creations/demos/shape_debug/CMakeLists.txt:28-39`).
- Helper functions live in `cmake/ir_functions.cmake`, `include()`d at
  `CMakeLists.txt:52-53`. New helpers go there, matching the existing
  `irreden_lua_codegen` / `IR_copyDLL` style.
- The per-demo asset target copies three dirs into `$<TARGET_FILE_DIR>`:
  `engine/render/data` → `data/`, `engine/data` → `data/`,
  `engine/render/src/shaders` → `shaders/`, plus a per-file `config.lua`
  → `scripts/`.
- **macOS metallib gap**: the compiled Metal lib is produced at
  `${PROJECT_BINARY_DIR}/shaders/default.metallib`
  (`engine/render/CMakeLists.txt:207-227`), *not* under each demo's dir.
  The per-demo copy grabs `engine/render/src/shaders` (source `.metal`),
  which does **not** include `default.metallib`. A macOS bundle MUST
  include the compiled lib or the Metal backend can't load shaders.

## Approach (one approach, picked)

**Decision: script-driven custom target, NOT CPack.** CPack needs
`install(TARGETS/DIRECTORY/FILES)` rules that would re-declare the asset
list and add component-config overhead, and the private game target can't
get public-repo install rules. Since the runtime layout is already a flat
exe-relative tree, a custom target that *stages the known pieces and zips
them with `cmake -E tar`* is simpler, needs no external zip tool, and is
reusable for any creation (including the private game). `cmake -E tar
--format=zip` ships with CMake on all three platforms.

### Phase 1 — `ir_bundle_assets(target [SCRIPTS f1 f2 ...])`

New function in `cmake/ir_functions.cmake` factoring the duplicated asset
copy. Model it on the existing `add_custom_target(<Name>Assets ...)` +
`add_dependencies` pattern:

```cmake
# Copies engine-common assets next to <target>'s executable so it runs
# from its own directory (ir_engine.hpp init() sets CWD = exe dir).
# Optional SCRIPTS files are copied into scripts/ (e.g. a creation's config.lua).
function(ir_bundle_assets target)
    cmake_parse_arguments(IRBA "" "" "SCRIPTS" ${ARGN})
    set(_dst $<TARGET_FILE_DIR:${target}>)
    add_custom_target(${target}_bundle_assets
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/engine/render/data ${_dst}/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/engine/data        ${_dst}/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${PROJECT_SOURCE_DIR}/engine/render/src/shaders ${_dst}/shaders
    )
    # macOS: include the compiled Metal lib (source .metal alone is not enough).
    if(APPLE)
        add_custom_command(TARGET ${target}_bundle_assets POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${PROJECT_BINARY_DIR}/shaders/default.metallib ${_dst}/shaders/default.metallib)
        add_dependencies(${target}_bundle_assets MetalShaders)  # confirm target name in engine/render/CMakeLists.txt
    endif()
    foreach(_s IN LISTS IRBA_SCRIPTS)
        get_filename_component(_b "${_s}" NAME)
        add_custom_command(TARGET ${target}_bundle_assets POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory ${_dst}/scripts
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_s} ${_dst}/scripts/${_b})
    endforeach()
    add_dependencies(${target} ${target}_bundle_assets)
endfunction()
```

Then migrate `creations/demos/shape_debug/CMakeLists.txt` to:
```cmake
ir_bundle_assets(IRShapeDebug SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/config.lua)
```
replacing lines 15-39 (keep the Windows `$<TARGET_RUNTIME_DLLS>` POST_BUILD
and the `<Name>Run` target as-is for now; the packaging target supersedes
manual running).

**Demo sweep**: migrate the remaining **public** demos in `creations/demos/*`
to call `ir_bundle_assets`. This is mechanical and identical per demo.
Build-verify a representative set on Linux: `IRShapeDebug` and the
**lighting** demo (shares ONE asset target across ~15 exe targets — call
`ir_bundle_assets` for the exe that owns the run dir; do not regress the
others). The private game demos (e.g. `day_night`) add `irreden_lua_codegen`
*before* the copy, so the helper MUST be designed to compose *after* codegen
rather than replace a creation's scripts wiring — but their migration is a
**game-repo follow-up** (the game tree is gitignored and not present in the
engine worktree), not part of this engine PR. If a demo's script handling
diverges too far to fit `SCRIPTS`, leave that demo on its current copy and
note it. **If the combined diff balloons past a comfortably-reviewable size,
carve the demo sweep into a separate follow-up PR** (file a no-label issue
per `docs/agents/TASK-FILING.md`); the must-ship core is the helper + its
use by `IRShapeDebug` + packaging + docs.

### Phase 2 — `ir_package_creation(target)`

New function in `cmake/ir_functions.cmake` emitting `package-<target>`:

```cmake
# Emits `package-<target>`: stages a clean self-contained bundle and zips it.
function(ir_package_creation target)
    set(_stage ${CMAKE_BINARY_DIR}/package/${target})
    set(_exedir $<TARGET_FILE_DIR:${target}>)
    set(_zip ${CMAKE_BINARY_DIR}/${target}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}.zip)
    set(_cmds
        COMMAND ${CMAKE_COMMAND} -E rm -rf ${_stage}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${_stage}
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${target}> ${_stage}/
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_exedir}/data    ${_stage}/data
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_exedir}/shaders ${_stage}/shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_exedir}/scripts ${_stage}/scripts
    )
    if(WIN32)
        # $<TARGET_RUNTIME_DLLS> covers linked deps but NOT the MinGW runtime;
        # those sit next to g++ (creations/CLAUDE.md §Gotchas).
        get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
        list(APPEND _cmds
            COMMAND ${CMAKE_COMMAND} -E copy -t ${_stage} $<TARGET_RUNTIME_DLLS:${target}>
            COMMAND ${CMAKE_COMMAND} -E copy
                ${_mingw_bin}/libgcc_s_seh-1.dll
                ${_mingw_bin}/libstdc++-6.dll
                ${_mingw_bin}/libwinpthread-1.dll
                ${_stage})
    endif()
    list(APPEND _cmds
        COMMAND ${CMAKE_COMMAND} -E tar cf ${_zip} --format=zip .)
    add_custom_target(package-${target}
        ${_cmds}
        WORKING_DIRECTORY ${_stage}
        DEPENDS ${target}
        COMMENT "Packaging ${target} -> ${_zip}"
        USES_TERMINAL)
endfunction()
```
Notes for the worker:
- The `tar` runs with `WORKING_DIRECTORY ${_stage}` and packs `.` so the
  zip's top level is the runtime files (no absolute paths inside).
- `COMMAND_EXPAND_LISTS` is needed on the `$<TARGET_RUNTIME_DLLS>` command
  (generator-expression list) — add it to that `add_custom_target` if the
  DLL copy expands to multiple files.
- macOS dynamic deps: most engine third-party libs are static, but confirm
  whether SDL/GLFW link dynamically; if so, copy the `.dylib`(s) into the
  stage and set the exe's rpath to `@executable_path` (build-time
  `set_target_properties(... INSTALL_RPATH "@executable_path")`). If all
  deps are static, nothing extra is needed beyond the metallib.

Wire the reference instance in `creations/demos/shape_debug/CMakeLists.txt`:
```cmake
ir_package_creation(IRShapeDebug)
```

### Phase 3 — Docs

Add a "Packaging a self-contained bundle" section to
`docs/agents/BUILD.md`: one-command invocation
`cmake --build build --target package-<TargetName>` → produces
`build/<Target>-<platform>-<arch>.zip`; per-platform notes (Windows MinGW
DLLs auto-included; macOS metallib auto-included; Linux relies on a recent
system glibc — nice-to-have).

## Affected files

- `cmake/ir_functions.cmake` — add `ir_bundle_assets()` + `ir_package_creation()`.
- `creations/demos/shape_debug/CMakeLists.txt` — migrate to `ir_bundle_assets`, add `ir_package_creation(IRShapeDebug)` (reference + fleet-verifiable instance).
- `creations/demos/*/CMakeLists.txt` — migrate remaining public demos to `ir_bundle_assets` (mechanical sweep; may be carved to a follow-up PR if oversized).
- `docs/agents/BUILD.md` — packaging section.
- *(game-repo follow-up, NOT this engine PR)* the game's demo + `IRGame` `CMakeLists.txt` — migrate to `ir_bundle_assets` and wire `ir_package_creation(IRGame)`.

## Acceptance criteria

- `cmake --build build --target package-IRShapeDebug` on Linux produces
  `build/IRShapeDebug-Linux-x86_64.zip` (one command, host platform).
- Unzip into a fresh directory **outside the repo/build tree**, run the
  exe from there → it loads palettes/shaders/config and renders (flat
  exe-relative layout means CWD=exe dir resolves all assets). This is the
  fleet's Linux confidence check.
- macOS + Windows produce a runnable bundle — **human-verified** (the
  fleet env is Linux-only; the issue requires mac+windows verification by
  a human). The CMake is written so the bundle includes the MinGW runtime
  (Windows) and `default.metallib` (macOS).
- The asset-copy duplication is replaced by `ir_bundle_assets` for at
  least `IRShapeDebug` and the migrated demos.

## Gotchas

- **macOS metallib gap** (above): source `.metal` ≠ compiled
  `default.metallib`; the helper must copy the compiled lib and depend on
  the Metal-shaders build target. Confirm that target's exact name in
  `engine/render/CMakeLists.txt`.
- **MinGW runtime DLLs** are NOT in `$<TARGET_RUNTIME_DLLS>` — derive
  their dir from `CMAKE_CXX_COMPILER` rather than hardcoding `C:\msys64\...`.
- **Lighting demo** shares one asset target across ~15 exe targets — don't
  regress it during migration.
- **Game demos** add `irreden_lua_codegen` before the copy — `ir_bundle_assets`
  must compose after codegen, not replace its scripts wiring.
- `$<TARGET_RUNTIME_DLLS>` generator-expression lists need
  `COMMAND_EXPAND_LISTS` on the custom target.
- Don't tar a demo's raw `CMAKE_CURRENT_BINARY_DIR` (it contains
  `CMakeFiles/`, `Makefile`, etc.) — stage the known runtime pieces into a
  clean `package/<target>/` dir first.
- **Cross-repo**: the game's `ir_package_creation(IRGame)` wiring is a
  *game-repo follow-up*, not part of this engine PR. Note it in the PR body
  so the game side knows the mechanism is ready.
- If the macOS metallib/rpath investigation widens into a debugging
  session, escalate sonnet→opus per role-worker step 8a.
