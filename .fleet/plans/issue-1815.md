# Plan: Packaging target — one-command self-contained per-platform game bundle

- **Issue:** #1815
- **Model:** sonnet
- **Date:** 2026-06-14

## Scope

Add a **one-command packaging target** that zips a self-contained,
per-platform bundle (exe + `data/` + `shaders/` + `scripts/` + runtime
libs) which runs by double-click on a clean machine, and **generalize**
the per-demo asset-copy `add_custom_target(<Name>Assets …)` pattern into
a reusable `cmake/ir_functions.cmake` helper so creations stop
copy/pasting it.

The deliverable PR is scoped to the **Linux-verifiable mechanism** plus
**two engine** reference consumers and docs. The macOS/Windows
**clean-box verification**, the **game-side** adoption (the game lives in
the private `creations/game/` subrepo — not modifiable from an engine PR;
see Gotchas), and the full remaining-target rollout are recommended
follow-ups (see "Cross-platform / follow-ups").

## Verified current state

- **The build dir is already a runnable bundle.** Each demo sets
  `set(IR_<NAME>_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})` and
  `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${IR_<NAME>_RUNTIME_DIR})`, then
  copies assets to `${RUNTIME_DIR}/data` and `${RUNTIME_DIR}/shaders`
  (`creations/demos/shape_debug/CMakeLists.txt:1-2,28-39`). So the exe
  and `data/`/`shaders/`/`scripts/` are **siblings** in one flat dir.
- **Asset resolution is exe-dir-relative — no code change needed.**
  `IREngine::init(argv0)` canonicalizes `argv[0]`, sets cwd to the exe
  directory, and resolves bare `scripts/`/`data/`/`shaders/` paths from
  there (`engine/include/irreden/ir_engine.hpp:34-40,59-70`; shader
  constants are bare relative paths in
  `engine/render/include/irreden/render/shader_names.hpp`). A zip of the
  flat runtime dir therefore double-clicks correctly with **zero**
  runtime-path changes — the bundle just needs the assets + runtime libs
  next to the exe.
- **The asset-copy block is copy/pasted across ~27 targets.** Canonical:
  `creations/demos/shape_debug/CMakeLists.txt:28-39` (the
  `IRShapeDebugAssets` target) and re-duplicated in the `…Run` target
  (lines 43-56). Game twin:
  `creations/game/irreden/demos/day_night/CMakeLists.txt:43-51`. Each
  copies `engine/render/data` + `engine/data` → `<rt>/data` and
  `engine/render/src/shaders` → `<rt>/shaders`; scripts are synced
  per-file via `copy_if_different` (`config.lua`, etc.). The game's
  `irreden/demos/day_night/CMakeLists.txt` carries the identical pattern
  (per the issue body) — but it lives in the **private game subrepo**, so
  it is converted in a follow-up **game-repo** PR, not here.
- **No packaging infrastructure exists.** Zero `install()`,
  `include(CPack)`, or `CPACK_*` anywhere in the tree.
- **Reusable-helper precedent:** `cmake/ir_functions.cmake` already hosts
  `irreden_lua_codegen()` (the model for a parameterized helper) and
  `IR_copyDLL(target dllName sourceDir)` gated on `IR_isWindows` (lines
  237-253) — reuse it for the MinGW DLLs.
- **Platform runtime deps:**
  - Linux — `BUILD_SHARED_LIBS OFF` by default
    (`CMakeLists.txt:27-34`), so exes are statically self-contained.
  - Windows — `$<TARGET_RUNTIME_DLLS:…>` POST_BUILD copy
    (`shape_debug/CMakeLists.txt:6-13`) does **not** include the MinGW
    runtime (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
    `libwinpthread-1.dll`); they live in `C:\msys64\mingw64\bin`
    (`creations/CLAUDE.md:59-61`).
  - macOS — Metal shaders compile to
    `${PROJECT_BINARY_DIR}/shaders/default.metallib`
    (`engine/render/CMakeLists.txt:160-227`) but are **not** currently
    bundled into a creation's `shaders/`; no rpath setup exists.
- **Registration:** creations are `add_subdirectory()`'d flat in
  `creations/CMakeLists.txt` (engine) and `creations/game/CMakeLists.txt`
  (game). Presets: `linux-debug` / `windows-debug` / `macos-debug`
  (`CMakePresets.json`).

## Approach (script-driven helper — chosen)

**CPack was considered and rejected:** CPack's `install(...)` prefix-tree
model (`bin/`, `share/`) fights the engine's flat exe-sibling layout, and
per-target component zips across 27 demos are awkward. Since the build
dir already *is* the bundle layout, a reusable helper + `cmake -E tar`
is simpler, per-target, and directly satisfies deliverable #2.

1. **`irreden_add_creation_assets(<target> [SCRIPTS f1 f2 …])`** in
   `cmake/ir_functions.cmake`. Encapsulates the existing pattern: create
   `<target>Assets` copying `engine/render/data` + `engine/data` →
   `$<TARGET_FILE_DIR:target>/data` and `engine/render/src/shaders` →
   `…/shaders`; sync each `SCRIPTS` file via `copy_if_different` into
   `…/scripts/`; on macOS also copy
   `${PROJECT_BINARY_DIR}/shaders/default.metallib` into `…/shaders/`;
   `add_dependencies(<target> <target>Assets)`. Use
   `$<TARGET_FILE_DIR:target>` so it works regardless of each demo's
   `RUNTIME_DIR` variable name.

2. **`irreden_add_package_target(<target> [BUNDLE_NAME name])`** in
   `cmake/ir_functions.cmake`. Creates `<target>Package` that depends on
   `<target>` (+ its assets), then:
   - **Windows:** copy `$<TARGET_RUNTIME_DLLS:target>` next to the exe,
     plus the 3 MinGW DLLs via the existing `IR_copyDLL(...)` resolving
     the MinGW bin dir from the compiler
     (`get_filename_component(_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)`,
     fallback `C:/msys64/mingw64/bin`).
   - **macOS:** ensure `default.metallib` is present in `shaders/`
     (handled by the assets helper); set
     `INSTALL_RPATH "@executable_path"` on the target and verify via
     `otool -L` that no non-system dylib remains (documented check).
   - **Linux:** nothing extra (static).
   - Then `cmake -E tar cfv
     ${CMAKE_BINARY_DIR}/dist/<bundle>-<platform>.zip --format=zip .`
     run with `WORKING_DIRECTORY $<TARGET_FILE_DIR:target>`, where
     `<platform>` ∈ {`linux`,`windows`,`macos`} from `CMAKE_SYSTEM_NAME`.
   - Register the target in a top-level aggregate `package` custom target
     (in `creations/CMakeLists.txt`) so `--target package` builds all
     registered bundles.

3. **Convert two engine reference consumers** to the helpers:
   `creations/demos/shape_debug/CMakeLists.txt` (has a `config.lua`
   script — exercises the `SCRIPTS` arg) and one more engine demo with a
   different/empty script set, e.g.
   `creations/demos/day_cycle/CMakeLists.txt`, to prove the helper
   generalizes. Keep each demo's existing `…Run` target working (it
   re-copies assets — have it depend on `<target>Assets` instead of
   inlining the copies, or leave `…Run` untouched if lower-risk).
   **Not** the game demo — `creations/game/` is the private game subrepo
   and is converted in a separate game-repo PR (follow-up).

4. **Document** the one-command invocation in `docs/agents/BUILD.md`:
   `fleet-build --target IRShapeDebugPackage` →
   `build/<preset>/.../dist/IRShapeDebug-linux.zip`; unzip + double-click
   (or `./IRShapeDebug`) on a clean box.

## Affected files

- `cmake/ir_functions.cmake` — add `irreden_add_creation_assets()` +
  `irreden_add_package_target()`.
- `creations/demos/shape_debug/CMakeLists.txt` — use both helpers
  (reference engine consumer, with `SCRIPTS config.lua`).
- `creations/demos/day_cycle/CMakeLists.txt` (or another engine demo) —
  second reference consumer.
- `creations/CMakeLists.txt` — top-level `package` aggregate target.
- `docs/agents/BUILD.md` — document the one-command packaging invocation.

## Acceptance criteria

- `fleet-build --target IRShapeDebugPackage` produces
  `dist/IRShapeDebug-linux.zip` containing the exe + `data/` + `shaders/`
  + `scripts/` in the flat exe-sibling layout.
- Unzipped into a fresh dir on the Linux fleet box (no repo, cwd = unzip
  dir), `./IRShapeDebug --auto-screenshot` launches and loads its assets
  (clean-box-equivalent; `IREngine::init(argv0)` sets cwd to the exe dir,
  so it does not depend on the original build path).
- Both reference demos build green and still run from the build tree
  (existing `…Run` flow unbroken).
- The Windows / macOS code paths are implemented per the per-platform
  steps above; their **clean-box verification** is tracked as the
  cross-platform follow-up (see below) — not a Linux-worker gate.

## Gotchas

- **No runtime-path code changes.** `IREngine::init(argv0)` already sets
  cwd to the exe dir; the flat exe-sibling bundle is correct as-is. Do
  not add path-rewrite logic.
- **Use `$<TARGET_FILE_DIR:target>`, not each demo's `RUNTIME_DIR`
  variable** — variable names differ per demo
  (`IR_SHAPE_DEBUG_RUNTIME_DIR`, `IR_DAY_NIGHT_RUNTIME_DIR`, …); the
  generator expression is uniform.
- **Don't regress the `…Run` targets** — they re-copy assets every run;
  keep them functional.
- **Windows MinGW DLLs** (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
  `libwinpthread-1.dll`) are **not** in `$<TARGET_RUNTIME_DLLS>`; pull
  from the MinGW bin dir (`creations/CLAUDE.md:59-61`). Reuse
  `IR_copyDLL()`.
- **macOS:** bundle `default.metallib` into `shaders/`
  (`engine/render/CMakeLists.txt:207-227`) and set
  `INSTALL_RPATH "@executable_path"`; confirm via `otool -L` there are no
  non-system dylib deps (the macOS preset may not be fully static).
- **`cmake -E tar … --format=zip`** needs no external zip tool and is
  cross-platform — prefer it over a shell `zip`.
- This PR converts **2** engine asset-copy sites; rolling the helper
  across the rest is mechanical but each demo's `SCRIPTS` list differs —
  handle per-demo in the rollout follow-up, not blanket sed.
- **`creations/game/` is the private game subrepo** (gitignored in the
  engine repo). The engine PR adds the helper to `cmake/ir_functions.cmake`
  (which the game build already consumes as a user project) but must
  **not** touch any `creations/game/` file. Game-demo conversion + the
  game bundle are a separate **game-repo** task.

## Cross-platform / follow-ups (recommend the human file)

The Linux fleet env cannot satisfy the issue's "verified on macOS and
Windows" criterion. Recommend two follow-ups (file per TASK-FILING.md,
no labels — left for the human/architect, not pre-filed here):

1. **`[opus]` packaging: macOS + Windows clean-box bundle verification +
   runtime-lib bundling** — run the Windows MinGW-DLL + macOS
   metallib/rpath paths on the real boxes, double-click-verify the
   unzipped bundle, fix dylib/rpath gaps. Blocked by #1815.
2. **`[sonnet]` packaging: roll `irreden_add_package_target` across the
   remaining engine demos/creations** — mechanical conversion, per-demo
   `SCRIPTS` handling. Blocked by #1815.
3. **`[sonnet]` (game repo) packaging: adopt `irreden_add_creation_assets`
   + `irreden_add_package_target` in the game's demos** (`day_night`,
   `unit_movement`, …) + produce the game bundle — filed on
   `jakildev/irreden`. Blocked by #1815.
