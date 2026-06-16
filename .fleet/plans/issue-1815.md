# Plan: Packaging target — one-command self-contained per-platform game bundle

- **Issue:** #1815
- **Model:** opus
- **Date:** 2026-06-14

## Scope

Add **two reusable CMake helpers** to `cmake/ir_functions.cmake` and wire
them onto a flagship target so that one command produces a self-contained,
per-platform, double-clickable bundle (exe + `data/` + `shaders/` +
`scripts/` + runtime libs):

1. `irreden_bundle_assets(target ...)` — generalize the per-demo asset-copy
   boilerplate (deliverable #2) so it is defined once instead of
   hand-copied across 22 demo `CMakeLists.txt`.
2. `irreden_package_target(target ...)` — assemble a clean staging dir and zip
   it into `<target>-<platform>-<arch>.zip`, bundling the platform runtime
   deps (deliverables #1, #3).
3. Document the one-command invocation in `docs/agents/BUILD.md`
   (deliverable #4).

**Single engine task, opus class.** See "Decomposition considered" below
for why this is not split and why the model is bumped from the filer's
suggested `sonnet`.

## Premise — relocatability is ALREADY solved (no engine code change)

`IREngine::init(argv0)` (`engine/include/irreden/ir_engine.hpp:63-70`)
already does `std::filesystem::current_path(exeDir)` and resolves
`scripts/`, `data/`, `shaders/` relative to the executable. The source
comment is explicit: *"All relative engine paths (shaders/, data/) resolve
from the exe directory."* A folder of `{exe, data/, shaders/, scripts/,
runtime libs}` is therefore **already relocatable and runs by double-click**.

Consequence: this is a **pure CMake/packaging task**. Do NOT add a second
runtime path-resolution mechanism — the engine already does the right thing.

## Approach (committed, in order)

### Step 1 — `irreden_bundle_assets(target [SCRIPTS f1 f2 ...])` helper

In `cmake/ir_functions.cmake`, add a function that reproduces the exact
current per-demo layout (so the migration is byte-for-byte behavior
preserving):

- **Windows runtime DLLs:** POST_BUILD copy of
  `$<TARGET_RUNTIME_DLLS:${target}>` into `$<TARGET_FILE_DIR:${target}>`
  (the existing shape at `creations/demos/shape_debug/CMakeLists.txt:6-13`).
  Gate on `if(WIN32)` — the unconditional form the demos already use
  (`shape_debug/CMakeLists.txt:6`). Note: the existing `IR_copyDLL` helper
  (`cmake/ir_functions.cmake:237-253`) gates on `IR_isWindows`, but that
  variable is set `PARENT_SCOPE` (line 80) only in scopes that called
  `IrredenEngine_setSystemCompileDefinitions` — so it is not reliably in
  scope inside a standalone helper. Prefer `WIN32` / `APPLE` /
  `UNIX AND NOT APPLE` for the platform branches.
- **Asset directories:** a `${target}Assets` custom target that
  `copy_directory`s `${PROJECT_SOURCE_DIR}/engine/render/data` and
  `${PROJECT_SOURCE_DIR}/engine/data` → `<exedir>/data`, and
  `${PROJECT_SOURCE_DIR}/engine/render/src/shaders` → `<exedir>/shaders`
  (the shape at `shape_debug/CMakeLists.txt:28-39`), then
  `add_dependencies(${target} ${target}Assets)`.
- **Scripts:** parse a `SCRIPTS` multi-value arg with
  `cmake_parse_arguments` (mirror `irreden_lua_codegen` at
  `cmake/ir_functions.cmake:167-234`); for each named file,
  `make_directory <exedir>/scripts` + `copy_if_different` into it
  (replacing the per-demo `config.lua` / `main.lua` copy blocks).
- Use `<exedir> = $<TARGET_FILE_DIR:${target}>` consistently.

Then **migrate every engine demo** (`creations/demos/*/CMakeLists.txt`,
~22 files) to a single `irreden_bundle_assets(IRFoo SCRIPTS config.lua ...)` call,
deleting the duplicated `add_custom_command`/`add_custom_target` blocks.
This is mechanical and behavior-preserving; the build-tree asset layout
must be identical pre/post (diff a built demo's runtime dir before/after to
confirm). Do NOT touch the `IR<Name>Run` targets' semantics — fold their
asset-copy duplication into the same helper if convenient, but keep the
`WORKING_DIRECTORY` + `USES_TERMINAL` run behavior.

### Step 2 — `irreden_package_target(target [SCRIPTS ...])` helper

In `cmake/ir_functions.cmake`, add a function that creates a
`${target}Package` custom target which:

1. Assembles a clean staging dir
   `${CMAKE_BINARY_DIR}/package/${target}/` containing the exe +
   `data/` + `shaders/` + `scripts/` (reuse the `irreden_bundle_assets` layout;
   `DEPENDS ${target} ${target}Assets`). `remove_directory` then
   `make_directory` first so re-packaging is clean.
2. Bundles platform runtime libs INTO the staging dir next to the exe:
   - **Windows:** copy `$<TARGET_RUNTIME_DLLS:${target}>` **plus** the
     MinGW runtime trio `libgcc_s_seh-1.dll`, `libstdc++-6.dll`,
     `libwinpthread-1.dll`. These are NOT in `$<TARGET_RUNTIME_DLLS>`
     (documented gotcha, `creations/CLAUDE.md:58-61`). Resolve their
     directory from the compiler:
     `get_filename_component(IR_MINGW_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)`
     and copy from there.
   - **macOS:** set the target's `INSTALL_RPATH` to `@executable_path` with
     `BUILD_WITH_INSTALL_RPATH ON` (so relocated dylibs resolve next to the
     exe), and copy any dynamically-linked third-party dylibs (glfw,
     ffmpeg) into the staging dir. Determine linkage first
     (`otool -L $<TARGET_FILE:${target}>` at configure-time is not
     available; instead key off the same dep targets the build links —
     prefer copying `$<TARGET_FILE:glfw>` etc. for the imported targets
     that are SHARED). System frameworks (Metal, Cocoa, etc.) need NO
     bundling. The precompiled `shaders/default.metallib` is produced into
     `${PROJECT_BINARY_DIR}/shaders/` by `engine/render/CMakeLists.txt` and
     needs no special packaging handling: the Metal backend uses
     `newLibraryWithSource` (runtime compilation from `.metal` source
     files), so `default.metallib` is NOT loaded at runtime. The shader
     `copy_directory` copies `.metal` source files from the source tree
     (`engine/render/src/shaders`) — those are what the runtime path
     actually needs.
   - **Linux:** set target `INSTALL_RPATH "$ORIGIN"`; copy third-party
     `.so`s into the staging dir.
3. Zips the staging dir:
   `cmake -E tar cfv ${CMAKE_BINARY_DIR}/${target}-<platform>-<arch>.zip
   --format=zip .` with `WORKING_DIRECTORY` = the staging dir.
   `tar --format=zip` is available (project min CMake is 3.28, file
   `CMakeLists.txt:1`). Derive `<platform>-<arch>` from
   `CMAKE_SYSTEM_NAME` + `CMAKE_SYSTEM_PROCESSOR`
   (e.g. `linux-x86_64`, `windows-x86_64`, `macos-arm64`).

**Use `cmake -E tar`, not CPack.** CPack's `install()`-prefix model fights
the per-platform dep bundling and the exe-relative asset layout; a
script-driven custom target gives full control and is simpler for a time-boxed deliverable.
Record this decision in the helper's leading comment.

### Step 3 — Wire a flagship target + note the game consumer

- Call `irreden_package_target(IRShapeDebug SCRIPTS config.lua)` (in
  `creations/demos/shape_debug/CMakeLists.txt`) so the mechanism is
  exercised and verifiable on the Linux fleet host.
- The **game** is a private creation in a separate repo, pulled in via
  `IRREDEN_USER_PROJECTS`. Its packaging wiring (calling
  `irreden_package_target(<gameExe> ...)`) is **follow-up work in the game
  repo**, out of scope for this engine PR. State this in the PR body and
  file the game-side follow-up per TASK-FILING.md if not already tracked.

### Step 4 — Document (deliverable #4)

In `docs/agents/BUILD.md`, add a "Packaging a distributable bundle"
subsection: `cmake --build <build-dir> --target <Target>Package` →
produces `<Target>-<platform>-<arch>.zip` in the build dir; note the
Windows MinGW-DLL inclusion and the macOS `@executable_path` rpath
handling, and that the unzipped folder runs by double-click on a clean box.

## Affected files

- `cmake/ir_functions.cmake` — add `irreden_bundle_assets()` and
  `irreden_package_target()` (alongside the existing `IR_copyDLL` /
  `irreden_lua_codegen` helpers).
- `creations/demos/*/CMakeLists.txt` (~22 files) — migrate to
  `irreden_bundle_assets()`; add `irreden_package_target(IRShapeDebug ...)` to
  shape_debug.
- `docs/agents/BUILD.md` — document the package target.

## Acceptance criteria

- On the Linux fleet host:
  `cmake --build <build> --target IRShapeDebugPackage` produces
  `IRShapeDebug-linux-x86_64.zip` in the build dir.
- Unzip that into a fresh `/tmp` path (no repo, no build-tree CWD) and run
  the exe **from that folder** (`./IRShapeDebug --auto-screenshot` or
  `--timeout N`) — it launches and loads its `data/`/`shaders/`/`scripts/`
  (proving exe-relative resolution + complete asset capture).
- After the helper migration, every engine demo still builds and its
  runtime dir layout is byte-identical to before (behavior-preserving
  refactor).
- Windows + macOS bundle correctness is verified via **cross-host smoke or
  the human** (the Linux fleet host cannot run those toolchains); the PR
  ships Linux-verified with the Windows/macOS branches written against the
  documented gotchas. Do NOT claim macOS/Windows "done" from a Linux run.

## Gotchas

- **Relocatability is already handled** by `IREngine::init` (cwd→exeDir).
  Do not add a second path-resolution path.
- **Windows `$<TARGET_RUNTIME_DLLS>` omits the MinGW runtime trio**
  (`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`) — copy
  them explicitly from the compiler bin dir (`creations/CLAUDE.md:58-61`).
- **macOS Metal shaders** — the engine uses `newLibraryWithSource`
  (runtime compilation from `.metal` source files); `default.metallib`
  is NOT loaded at runtime. The existing shader `copy_directory` copies
  `.metal` source files from the source tree
  (`engine/render/src/shaders`) — that is what the runtime path needs.
  The precompiled `default.metallib` in `${PROJECT_BINARY_DIR}/shaders/`
  needs no special packaging handling.
- **Don't reshuffle** `creations/demos/CMakeLists.txt` subdir order
  (`creations/demos/CLAUDE.md` gotcha) — only edit individual demo files.
- The migration touches ~22 files; keep each demo's `SCRIPTS` list exactly
  matching what it copies today (some demos copy `main.lua` + extra `.lua`,
  not just `config.lua`) — audit each before deleting its block.
- `cmake -E tar --format=zip` needs CMake ≥3.18; project min is 3.28, so
  this is safe — no version bump required.

## Decomposition considered (why one opus task, not a sonnet split)

The work splits naturally into a mechanical asset-helper refactor (the
22-demo migration — sonnet-tier) and a cross-platform packaging target
(Windows MinGW DLLs, macOS dylib/rpath relocation, zip tooling —
judgment-heavy, opus-tier). I kept it as **one opus task** because:

- The two helpers are one cohesive contract — `irreden_package_target` consumes
  the `irreden_bundle_assets` layout; splitting forces a merge → unblock →
  re-pick latency on a **release-critical** path.
- Both edit the same file (`cmake/ir_functions.cmake`) plus overlapping
  demo `CMakeLists.txt`; flat siblings on the same surface risk the
  conflicting-parallel-PR failure mode (cf. #1370). A single PR avoids it.
- Relocatability being already-solved collapses the risk surface to pure
  CMake, keeping the combined task tractable in one PR.

Bumped from the filer's suggested `[sonnet]` to **`[opus]`** because the
cross-platform dep bundling (MinGW runtime set, macOS `install_name`/rpath
relocation, packaging-tool choice) is design judgment a sonnet worker would
likely `fleet:design-block` on. If the human prefers a sonnet-friendly
split, the clean cut is: P1 `irreden_bundle_assets` migration (sonnet) →
P2 `irreden_package_target` + docs (opus), as a `file-epic` Blocked-by chain.
