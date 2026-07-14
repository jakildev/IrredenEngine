# GPU stage timing: reading contract + dispatch cost model

Source of truth for (1) what the GPU STAGES timers actually measure and how
to read the table without misattributing cost, and (2) the measured GPU
dispatch cost model that perf plans must build on. Written after two
independent perf plans (#2256 → PR #2271, #2258 → PR #2266) were refuted by
measurement mid-implementation for the same two reasons: modeling
"dispatched-but-early-returning invocations" as reclaimable cost, and
reading bundled/unwired timer rows as if they were per-dispatch
measurements.

Consumers: anyone planning a `fleet:needs-plan` perf issue against the
render pipeline, the `optimize` skill flow, and reviewers of dispatch-shape
changes (occupied-only lists, indirect dispatch, subdivision caps).

## 1. What the timers measure

- **One measurement per tagged `SystemId`, bracketing the whole tick.**
  `IRRender::tagGpuStage(systemId, "<name>")` registers a system against a
  row of `gpuStageRegistry()` (`engine/prefabs/irreden/render/
  gpu_stage_timing.hpp`); `GpuStageTimingObserver` writes a START timestamp
  before the tick and an END after it
  (`gpu_stage_timing_observer.hpp`).
- **The samples are genuine GPU-timeline times, not CPU encode times.** On
  Metal each pair is an `MTL::CounterSampleBuffer` attached at encoder
  boundaries (`engine/render/src/metal/metal_render_impl.cpp`): the stage's
  first encoder claims the start boundary and every subsequent encoder
  re-writes the end, so the resolved pair spans [first encoder starts on
  GPU, last encoder ends on GPU] (#1746). OpenGL uses the device timestamp
  pairs in `opengl_render_impl.cpp`. Readback is async (3 pairs in flight);
  the overlay shows the most recent *resolved* sample, and per-stage
  accumulators build the shutdown avg/min/max (#1738). A `finish()`-bracket
  legacy path exists for devices without timestamp support
  (`legacyFinishTiming_`).
- **Two registry rows have NO writer and always read 0.000:** `shapePass0`
  (folded into the `SHAPES_TO_TRIXEL` per-system bundle) and `shapeCompact`
  (reserved). They stay in the registry for overlay/Lua stability.
- **`canvasClear` / `voxelCompact` / `voxelStage1` / `voxelStage2` are
  intra-tick sub-rows (#2280).** `VOXEL_TO_TRIXEL_STAGE_1` is no longer tagged
  for the per-system observer; its per-canvas tick brackets each dispatch group
  with a `GpuSubStageScope`, so `voxelStage1` now measures the stage-1 dispatch
  ONLY and the other three rows carry the clear / compact / stage-2 costs. The
  old whole-tick number = the sum of the four. (CPU `voxelStage1` stays the
  whole tick — an `IR_PROFILE_SCOPE` replaces the observer's CPU bracket.)
- **`shapePass1` is still a bundle** — it covers all of `SHAPES_TO_TRIXEL`.

### Reading rules

1. A **0.000 row** is either *unwired* (check the registry comment for the
   current writer list before quoting it) or *genuinely below the 0.005 ms
   display floor*. Distinguish before drawing conclusions — both mistakes
   have now been made in opposite directions (#2266 initially read unwired
   rows as measurements; verifying #2271 required confirming
   `resolvePerAxisScreenDepth` was NOT unwired).
2. A **bundle row** (`shapePass1`) cannot attribute cost to a sub-dispatch.
   The voxel path is no longer a bundle: since #2280, `voxelStage1` /
   `voxelCompact` / `canvasClear` / `voxelStage2` are per-dispatch GPU rows,
   so "stage-1's raster is the cost" IS answerable from `voxelStage1` alone
   (it measured ~65 ms of the ~140 ms at zoom 16 — see §2).
3. Per-system rows for single-purpose systems (`computeVoxelAO`,
   `computeSunShadow`, `lightingToTrixel`, `resolvePerAxisScreenDepth`,
   `trixelToFb`, …) are trustworthy per-stage measurements on both
   backends.

## 2. Measured dispatch cost model (Metal/macOS, 2026-07)

Facts established by probes on real scenes; treat as the default model until
a measurement says otherwise on a specific path:

- **Empty early-return invocations are effectively free** at the grid sizes
  the per-axis pipeline dispatches (~10⁵–10⁶ invocations): pre-#2273
  master's `RESOLVE_PER_AXIS_SCREEN_DEPTH` swept the full `3×(2W)(W+H)`
  per-axis grid — mostly empty cells hitting the top-of-kernel sentinel
  return — and read 0.00 ms (PR #2271). Corollary: converting a full-grid
  sweep whose kernel early-returns on empties into an occupied-only /
  indirect dispatch processes the same occupied cells and reclaims
  ~nothing. **Empirically confirmed twice on the same surface:** PR #2271
  predicted it from the timer read and stopped; PR #2273 independently
  built the indirect conversion and its own A/B measured **perf-neutral vs
  master** on Metal.
- **Shader-side early-return cannot reclaim dispatch-grid cost.** The launch
  grid is fixed CPU-side at `dispatchCompute` time; an invocation that
  returns immediately still launched. PR #2266 capped shadow-feeder
  micro-grids via early-return and measured no change in `voxelStage1`.
- **A runtime *uniform* mode-branch in a hot kernel is predicated, not
  skipped** — its instructions decode on every invocation even when the
  branch is never taken, so it taxes the kernel at all inputs. #2325 Step B
  v1 selected visible-vs-feeder via a `feederPass` uniform inside the shared
  stage-1 kernel and regressed zoom8 `voxelStage1` +16 %; a disarm probe
  (gate off, dispatch skipped) held the taxed number, proving the cost was
  shader-side predication of the strided divides, not dispatch overhead.
  Prefer a **compile-time `#if` specialization** from one shared source body:
  two thin wrappers `#define` the mode and `#include` the body, and each
  Metal variant registers in `threadgroupSizeForFunctionName` +
  `functionUsesImageAtomicScratch`. Specializing measured the tax away
  (zoom8 `voxelStage1` 5.97 → 5.33 ms, zoom16 win retained). See
  `docs/design/voxel-feeder-split.md` §"#2258 Step B".
- **The stage-1 raster body is not where the high-zoom cost lives — but the
  stage-1 *dispatch* still carries ~half of it.** Forcing *every* voxel to a
  single 1×1 micro-tap (a 256× per-invocation body reduction at zoom 16) left
  the whole-tick number unchanged within noise (~140–150 ms, IRPerfGrid wave
  scene, shadows on) — the raster BODY is not the cost. The #2280 sub-scopes
  then attributed the bundle (Metal, IRPerfGrid wave, shadows on): at zoom 16
  `voxelStage1` ~65 ms + `voxelStage2` ~76 ms carry essentially all of it,
  with `voxelCompact` ~0.06 ms and `canvasClear` ~0.01 ms negligible; at
  zoom 8, ~24 / ~28 / ~0.7 / ~0.1 ms. So the high-zoom cost is the stage-1 +
  stage-2 *dispatch-grid* cost (fixed CPU-side, body-independent), split
  roughly evenly — NOT compact, clear, or launch overhead.
- The per-axis yaw GPU delta (#2256) is therefore **occupied-cell-bound**,
  not invocation-count-bound. #2256 closed via #2273 with the delta
  unrecovered; its per-stage attribution uses the existing per-system
  timers (plan on #2281).

## 3. Rules for perf plans

1. **Attribute before re-architecting.** A plan that changes dispatch shape
   (occupied-only lists, indirect dispatch, caps, pass collapses) must cite
   a measurement showing the targeted cost exists *in the targeted
   sub-stage* — per-system rows for single-purpose systems, #2280 scopes
   for bundles. "The sweep is mostly empty, so it must be the cost" is the
   exact premise measurement has now refuted twice.
2. **Never model early-return invocations as reclaimable** without a probe.
   Cheap decisive probes that need no new infrastructure: read the
   per-system row of an equivalent-shaped sweep (the #2271 method); force a
   uniform body reduction and diff the row (the #2266 method); settled
   `--auto-profile` tables at two poses and diff per stage.
3. **State the noise bound.** The IRPerfGrid wave scene is run-to-run
   nondeterministic; single-run deltas inside ~±10 ms at the 140 ms scale
   are not findings. Rank stages by *deltas across poses/configs*, repeat
   runs, and prefer a quiet host when quoting absolute ms.

## 4. Migration status / open items

- **#2280** — LANDED: intra-tick sub-stage GPU scopes (`GpuSubStageScope`,
  `gpu_substage_timing.hpp`) wire the `voxelCompact` / `canvasClear` /
  `voxelStage2` rows and narrow `voxelStage1` to stage-1-only, on both
  backends. `VOXEL_TO_TRIXEL_STAGE_1` is untagged from the per-system observer
  (the sub-scopes reuse the freed attachment slot; Metal's clear blit is timed
  via a timestamp-aware `createBlitEncoder`). Coverage delta: the rotating-only
  per-axis voxel dispatch is not sub-scoped.
- **#2258** — unblocked by #2280; the lever keys on the attribution table
  (stage-1 + stage-2 dispatch-grid cost, ~evenly split) posted there.
- **#2281** — per-stage cardinal-vs-yaw delta table with existing timers
  (successor to #2256, which closed via the perf-neutral #2273); the lever
  decision is a design gate after the table exists.

## 5. What to verify when touching the timers

- Timing-only changes must be render-byte-identical with timing enabled and
  disabled.
- The timers-off cost stays one bool check per observer fire.
- After wiring any sub-stage scope: sum of sub-rows ≈ the enclosing
  per-system row (± measurement overhead) at a static pose, on both
  backends.
- Any change to which system writes a registry row updates the registry
  comment in `gpu_stage_timing.hpp` AND §1 of this doc — the registry
  comment is what stops the next misreading.
