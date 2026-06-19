# Plan: demos/cmake: migrate the remaining hand-rolled demo CMakeLists to irreden_bundle_assets

- **Issue:** #1888
- **Model:** sonnet
- **Date:** 2026-06-17

## Scope

Migrate the **22 demo CMakeLists** under `creations/demos/` that still hand-roll
asset staging to the `irreden_bundle_assets(<target> ...)` helper
(`cmake/ir_functions.cmake:277`), and drop the now-redundant inline
`copy_directory`/`copy_if_different` commands from each demo's `IR*Run` target.
`shape_debug` is already migrated (via #1815) and is the **canonical reference**
(`creations/demos/shape_debug/CMakeLists.txt`).

This is a **pure CMake refactor** — no C++/shader/runtime-behavior change. The
helper reproduces the hand-rolled layout **byte-for-byte** (per its header
comment at `ir_functions.cmake:276`), so the staged `data/`, `shaders/`,
`scripts/`, and any extra dirs must be identical before/after.

The 22 demos (every `creations/demos/*` except `shape_debug`):
`audio_playback`, `analytic_oracle`, `canvas_stress`, `chunk_streaming_smoke`,
`default`, `gpu_particles`, `day_cycle`, `ir_voxel_yaw`, `lowres_pan`,
`lua_perf_grid`, `lighting`, `lua_pipeline_demo`, `midi_keyboard`,
`metal_clear_test`, `modifier_demo`, `perf_grid`, `skeletal_demo`,
`sprite_demo`, `stateless_particles`, `ui_dockspace`, `z_yaw_rotation`,
`ui_widgets`.

## Approach

Per demo:

1. **Inventory the existing block.** Read the CMakeLists and identify:
   - the target name (`add_executable(<target> ...)` — note some are not `IR*`,
     e.g. `midi_keyboard` → `DemoMidiDevice`),
   - the SCRIPTS it stages (the `copy_if_different` sources — `config.lua`,
     `main.lua`, `commands.lua`, or a `scripts/<f>.lua`),
   - any **non-engine** EXTRA_DIRS (a `copy_directory` whose source is NOT
     `engine/render/data`, `engine/data`, or `engine/render/src/shaders`).

2. **Replace the whole hand-rolled staging section** — the per-script
   `set(IR_<NAME>_..._SRC/_DST ...)` vars, the
   `add_custom_command(OUTPUT ... copy_if_different ...)` blocks, the
   `if(WIN32) ... TARGET_RUNTIME_DLLS ...` block, the
   `add_custom_target(<T>Assets ...)`, and the `add_dependencies(<T> <T>Assets)`
   — with a single call:
   ```cmake
   irreden_bundle_assets(<target>
       [SCRIPTS <f1> <f2> ...]
       [EXTRA_DIRS <abs-src1> <reldst1> ...])
   ```
   - **SCRIPTS** entries resolve relative to `CMAKE_CURRENT_SOURCE_DIR` and are
     copied to `<exe>/scripts/<basename>`. A root `config.lua` →
     `SCRIPTS config.lua`; `audio_playback`'s `scripts/audio_demo.lua` +
     `config.lua` → `SCRIPTS scripts/audio_demo.lua config.lua`.
   - **EXTRA_DIRS** takes `(src dst)` pairs; the helper uses `src` **as-is**
     (it does NOT prefix it), so pass an absolute
     `${CMAKE_CURRENT_SOURCE_DIR}/<rel>`; `dst` is relative to the exe dir.
     Examples: `audio_playback` →
     `EXTRA_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/data/audio data/audio`;
     `perf_grid` → `EXTRA_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/configs configs`;
     `sprite_demo` → its texture/asset dir (read its existing pair).
   - **Keep unchanged**: the leading
     `set(IR_<NAME>_RUNTIME_DIR ${CMAKE_CURRENT_BINARY_DIR})` /
     `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ...)` lines, the `add_executable(...)`,
     and `target_link_libraries(...)`.

3. **Simplify the `IR*Run` target.** Drop the inline
   `copy_directory`/`copy_if_different` commands; keep only
   `COMMAND $<TARGET_FILE:<target>>`, `DEPENDS <target> <target>Assets`,
   `WORKING_DIRECTORY ...`, `USES_TERMINAL` — exactly like `shape_debug`'s
   `IRShapeDebugRun`. The `<target>Assets` dependency re-stages on every run
   (it's a no-`OUTPUT` custom target, always out-of-date), so **live script
   edits are still picked up without a C++ rebuild** — the behavior the inline
   copies provided is preserved.

4. **Do NOT add `irreden_package_target(...)`.** Packaging is #1815's per-demo
   concern and out of scope here.

Use `creations/demos/shape_debug/CMakeLists.txt` as the exact target-shape
reference throughout.

## Affected files

- `creations/demos/<demo>/CMakeLists.txt` for each of the 22 demos listed in
  Scope.
- `shape_debug` is already migrated — **do not touch it**.

## Acceptance criteria

- Every listed demo's CMakeLists uses `irreden_bundle_assets` and has **no**
  hand-rolled `add_custom_target(<T>Assets)` / engine `copy_directory` block and
  **no** inline `copy_*` commands in its `IR*Run` target.
- `simplify` Check 6 (added by PR #1893) passes on all migrated demos (it flags
  hand-rolled `copy_directory` blocks; it already passes `shape_debug`).
- Configure + build each migrated target succeeds; the exe-relative `data/`,
  `shaders/`, `scripts/` (+ any extra dir) are staged identically to before.
  Spot-check 3 representative demos by listing the staged dirs: a root-scripts
  one (`default`), a `scripts/`-subdir + extra-dir one (`audio_playback`), and
  an extra-dir one (`perf_grid`).
- No change to staged file contents/layout (byte-identical); no C++/shader edits.

## Gotchas

- **EXTRA_DIRS `src` is NOT prefixed by the helper** — pass
  `${CMAKE_CURRENT_SOURCE_DIR}/<rel>`. A bare relative path silently resolves
  against the build dir and copies nothing.
- **Ordering**: `audio_playback` stages `data/audio` *after* the engine `data`
  copy so it isn't clobbered. The helper appends EXTRA_DIRS after the engine
  `data`/`shaders` copies, so this ordering is preserved — keep audio under
  EXTRA_DIRS, not a separate pre-copy.
- **Non-`IR*` / host-gated targets**: `midi_keyboard`'s target is
  `DemoMidiDevice`; `audio_playback`/`midi_keyboard` may have host-gated runtime
  deps (audio/MIDI backend). The CMake migration is still valid by inspection +
  the byte-identical guarantee — if a demo's C++ won't build/run on the host,
  migrate its CMakeLists anyway and note the unverified runtime in the PR.
- **Don't touch the `set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ...)` lines** — the
  helper keys off `$<TARGET_FILE_DIR:target>`, which those lines set.
- **Scripts source location varies**: some demos stage scripts from a `scripts/`
  source subdir (`audio_playback`) vs the demo root (`default`, `canvas_stress`,
  `lua_pipeline_demo`). Derive SCRIPTS paths from each demo's existing
  `copy_if_different` sources — don't assume root.
- This is the **companion to PR #1893** (simplify Check 6 + doc callout): #1893
  prevents *new* hand-rolled blocks; this issue clears the *existing* 22. They
  are independent — #1893 need not merge first.
- The diff is large but uniform (~22 files, low-risk). One PR is fine and
  reviewable; split into two only if you prefer.
