# Plan: render: assert UB preconditions inside shared helpers (voxelDispatchGridForCount + audit)

- **Issue:** #1685
- **Model:** sonnet
- **Date:** 2026-06-13

## Scope

Turn the comment-documented `count > 0` precondition on the shared
`voxelDispatchGridForCount()` helper into an enforced `IR_ASSERT`, so a
future un-guarded caller fails loudly in debug instead of silently
producing a `(0,0)` grid on Apple Silicon (integer div-by-zero is a
SIGFPE on x86 Linux but silently returns 0 on Apple Silicon — the #1619
isolation repro ran straight through the UB on macOS producing
misleading black screenshots while the Linux run crashed). Then audit
`engine/prefabs/` + `engine/render/` for other shared-helper preconditions
of the same shape and assert those that are not already enforced.

Code-work split-out from #1649; the governing rule text lands separately
via the coding-improvement batch PR (`docs/agents/CLAUDE-BASELINE.md`
§"Encode contracts in code, not in comments").

## Verified current state (premise confirmed)

- `voxelDispatchGridForCount(int count)` lives in
  `engine/prefabs/irreden/render/voxel_dispatch_grid.hpp` (21 LOC,
  header-only `inline`). The UB is `IRMath::divCeil(count, groupsX)` where
  `groupsX = min(count, 1024)` — when `count == 0`, `groupsX == 0` and we
  divide by zero. Header currently includes ONLY `<irreden/ir_math.hpp>`.
- **6 call sites** (confirmed by grep, 5 system files + `voxel_frame_data.hpp`):
  1. `system_shapes_to_trixel.hpp:531` — **guarded** (`if (tileCount == 0)
     { gridXOut = 1; return 0; }` immediately above).
  2. `system_update_voxel_positions_gpu.hpp:249` — `divCeil(liveCount, 64)`.
  3. `system_voxel_to_trixel.hpp:311` — `divCeil(liveVoxelCount, 64)`.
  4. `system_voxel_to_trixel.hpp:349` — `divCeil(destCount, 64)`.
  5. `system_voxel_to_trixel.hpp:664` — `divCeil(effectiveVoxelCount, 64)`.
  6. `voxel_frame_data.hpp:55` (`buildVoxelFrameData`) — **clamped**
     (`voxelDispatchGridForCount(IRMath::max(liveVoxelCount, 1))`). Its
     comment (lines 49–55) documents the one legitimate `count == 0` path:
     lighting passes author frame data for canvases whose pool is EMPTY,
     with "no liveVoxelCount gate" — the #1619 SIGFPE. That path clamps to
     1 rather than guarding; `voxelCount_` still carries the honest 0 to
     gate shader-side work.
- **Sites 2–5 are de-facto guarded** (`count > 0` whenever they run): if
  any of them reached the helper with `count == 0` in normal operation,
  x86 Linux would already SIGFPE on every empty-scene frame — and it does
  not. So the only path that legitimately produces `count == 0` is
  `buildVoxelFrameData`'s lighting case, which is already clamped. The
  issue body's "callers guard `count > 0`" comment therefore holds for
  every caller; `buildVoxelFrameData` is the documented, justified clamp.

This makes `IR_ASSERT(count > 0)` (the issue's literal item-1 ask) a
**correct contract that never fires in valid use** — it only catches a
future caller that forgot to guard.

## Approach (one approach, picked — Option A: assert + keep the clamp)

1. In `engine/prefabs/irreden/render/voxel_dispatch_grid.hpp`:
   - Add `#include <irreden/ir_profile.hpp>` (provides `IR_ASSERT`).
   - Add as the **first statement** of `voxelDispatchGridForCount`:
     ```cpp
     IR_ASSERT(count > 0,
         "voxelDispatchGridForCount: count must be > 0 — division by zero "
         "is UB (silent on Apple Silicon)");
     ```
   - Trim the existing "Callers guard `count > 0` ... `count == 0` divides
     by zero here, same as it always has." comment to state the contract is
     now enforced at entry (e.g. "Callers must guarantee `count > 0`; the
     assert above enforces it. The one legitimate empty-pool path
     (`buildVoxelFrameData`, lighting on an empty canvas) clamps to 1
     before calling — see voxel_frame_data.hpp.").
2. **Keep** the `IRMath::max(liveVoxelCount, 1)` clamp in
   `voxel_frame_data.hpp:55` — do NOT fold it away. It is the one
   legitimate `count == 0` path; removing it would make the new assert fire
   on valid empty-canvas lit scenes (and reintroduce the #1619 SIGFPE in
   release, where the assert compiles out). Its existing comment already
   justifies it; no edit needed (optionally add a half-line noting the
   helper now asserts, so the clamp reads as the deliberate exception).
3. **Item-2 audit.** Run `git grep -inE "caller(s)? (guard|guarantee|must)"
   engine/prefabs engine/render` and add `IR_ASSERT` at function entry for
   any *un-asserted* shared-helper precondition whose violation is UB.
   Current sweep results (confirm they still hold; act only on new ones):
   - `component_voxel_pool.hpp:602` ("Caller guarantees every index
     satisfies kVoxelTransformStatic || < kMaxGpuVoxelTransforms") —
     **already asserted**: the `IR_ASSERT` loop enforcing exactly this
     invariant sits immediately below the comment (lines 603–611). No
     change; the comment is accurate.
   - `camera_controls.hpp:43` ("Callers must separately register
     CAMERA_SCROLL_ZOOM in INPUT") — a registration requirement, not a
     function-entry precondition assertable at the call. No change.
   - `render/CLAUDE.md:66` — documentation, not code. No change.
   So beyond the dispatch helper, the audit is expected to yield **no
   additional code change**; state this explicitly in the PR (paste the
   sweep output).

### Alternative considered (rejected)
"Total helper" — fold `IRMath::max(count, 1)` into the helper itself
(handle `count <= 0` centrally) and remove the `buildVoxelFrameData`
clamp. Rejected: it changes the macOS empty-dispatch behavior at sites 2–5
from `(0,0)` to `(1,1)` (those paths don't occur in practice, but it's an
unnecessary behavior change), and it contradicts the issue's stated
"assert UB preconditions" deliverable. Option A is the issue's literal ask
and the one real 0-path stays release-safe via the retained clamp. (Noted
for the human; the worker implements Option A.)

## Affected files

- `engine/prefabs/irreden/render/voxel_dispatch_grid.hpp` — add
  `ir_profile.hpp` include + `IR_ASSERT(count > 0, ...)` at entry; reword
  the precondition comment.
- `engine/prefabs/irreden/render/voxel_frame_data.hpp` — keep the clamp
  (optional one-line comment tweak only).

## Acceptance criteria

- `voxelDispatchGridForCount` asserts `count > 0` at entry with the
  specified message; the header includes the `IR_ASSERT`-providing header
  and still compiles.
- The `buildVoxelFrameData` `max(liveVoxelCount, 1)` clamp is retained
  (PR body states why folding was rejected).
- PR body pastes the full `git grep -inE "caller(s)? (guard|guarantee|
  must)" engine/prefabs engine/render` output and confirms no additional
  un-asserted function-entry UB precondition was found (or asserts any
  that were).
- Build green: `fleet-build --target IRShapeDebug`.
- A debug run of a normal scene (e.g. `IRShapeDebug`) does **not** trip the
  assert (confirms `count > 0` holds in practice). Run an empty/lit-canvas
  scene if readily available to exercise the clamped path.
- `render-verify` (or a `shape_debug` `--auto-screenshot` pass) shows **no
  visual regression** — Option A introduces no runtime behavior change for
  `count >= 1`, so output must be byte-identical to master.

## Gotchas

- **Include:** the header currently pulls ONLY `<irreden/ir_math.hpp>` and
  was deliberately kept lightweight (the #1422 note in
  `system_update_voxel_positions_gpu.hpp` calls out "no dependency on the
  heavy STAGE_1 system header"). Adding `<irreden/ir_profile.hpp>` is
  required for `IR_ASSERT`; confirm it does not create an include cycle or
  drag in heavy transitive headers. `ir_profile.hpp` is a low-level
  profiling/assert header used pervasively across engine headers, so this
  should be clean — but verify the build.
- **Macro arity:** `IR_ASSERT(x, en, ...)` is printf/fmt-style
  `(condition, "message", ...args)`. Two definitions exist
  (`ir_profile.hpp:88` active, `:127` no-op for release) — the message here
  takes no fmt args so a plain string literal is correct. `IR_ASSERT`
  compiles out in release, so this is a **debug-only** contract with zero
  release cost; release-time protection for the one known 0-path remains
  the retained `buildVoxelFrameData` clamp.
- **Do NOT fold the clamp away** (see Approach step 2) — the assert would
  then fire on valid empty-canvas lit scenes.
- This touches a render hot-path helper across 5 systems × 2 backends, but
  the change is semantics-preserving for `count >= 1`; `render-verify` is
  the safety net.
