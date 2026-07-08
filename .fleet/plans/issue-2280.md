# Plan: profile — intra-tick GPU sub-stage scopes (wire the reserved voxelStage1 sub-rows)

- **Issue:** #2280 (blocks #2258)
- **Model:** opus (Metal backend timestamp infrastructure + render hot-path system wiring)
- **Date:** 2026-07-08

## Scope

An intra-tick scoped GPU timing mechanism so a system can bracket individual
dispatch groups inside one tick, wiring the reserved `voxelStage1` sub-rows
(`voxelCompact` / `canvasClear` / `voxelStage2`) on **both** backends, then use
it to attribute the ~140 ms `voxelStage1` cost that #2258 measured but could not
localize (the per-`SystemId` timer bundles compact + clear + stage-1 + stage-2).

## Confirmed repro

PR #2266's measurement table (2026-07-06 NEEDS-DESIGN comment): `voxelStage1`
~139–152 ms at zoom 16 on the IRPerfGrid wave scene (Metal, shadows on),
invariant to a 256× stage-1 body reduction — the cost is inside the bundled
per-system scope but not in the stage-1 raster body. The bundle is structural:
one `GpuStageTimingObserver` pair per `SystemId`
(`gpu_stage_timing.hpp:196-209`), so compact/clear/stage-1/stage-2 are
indistinguishable today.

## Approach (picked): RAII sub-stage scope + a second sample-buffer attachment on Metal

1. `RenderDevice` gains scoped-timestamp entry points reusing the existing pair
   machinery (`createTimestampPair` / `writeTimestamp` / `readTimestampPairMs`),
   plus a Metal path that attaches the sub-scope's `MTL::CounterSampleBuffer` on
   **attachment index 1** of the compute-pass descriptor, leaving index 0 to the
   enclosing per-system pair (`createComputeEncoder`, `metal_render_impl.cpp`).
   Same sticky first-encoder-claims-start / last-encoder-wins-end semantics as
   #1746, scoped to the sub-window. OpenGL needs no structural change —
   `glQueryCounter`-style pairs nest freely in the command stream.
2. A small `IRRender::GpuSubStageScope(name)` RAII (prefab layer, next to
   `gpu_stage_timing_observer.hpp`) resolves the name against
   `gpuStageRegistry()` and brackets the enclosed dispatches; used inside
   `VOXEL_TO_TRIXEL_STAGE_1`'s tick around its four dispatch groups to fill
   `voxelCompact`, `canvasClear`, `voxelStage1` (now stage-1 only),
   `voxelStage2`. Async readback keeps the `kSamplesInFlight` pattern per scope;
   timers-off cost stays one bool check.
3. **Fallback (only if the second Metal attachment misbehaves on real
   hardware):** sub-scopes own attachment index 0 while active and the outer
   per-system row is computed as the **sum** of sub-scope samples for that
   system. Document whichever shape lands in the registry comment.
4. Measure: post the zoom 8 / zoom 16 wave-scene attribution table on #2258 (per
   this issue's acceptance criteria).

## Acceptance criteria

- GPU STAGES overlay shows non-zero `voxelCompact` / `canvasClear` /
  `voxelStage2` rows when frame timing is enabled, on both backends.
- Sum of sub-rows ≈ the old bundled `voxelStage1` value (± measurement overhead)
  at a test pose on at least one backend.
- Attribution table for the #2258 scenario (IRPerfGrid default wave scene, zoom 8
  and zoom 16, sun shadows on, `--auto-profile 100`+) posted on #2258, naming
  which sub-stage carries the ~140 ms.
- Byte-identical rendering with timing disabled and enabled (screenshot compare
  on one demo).

## Sibling / in-flight reconciliation

PR #2266 is being narrowed to the byte-identical `sunBakeFrustumUVBounds`
refactor only (architect direction on the PR) — no overlap with this surface.
#2256's plan explicitly does NOT need this issue (its stages are separate,
already-timed systems). #2258 is blocked by this issue and must not be
re-planned until the attribution table lands on its thread.

## Gotchas

- Wiring the sub-rows changes the meaning of the `voxelStage1` row (from bundle
  to stage-1-only) — update the registry comment in `gpu_stage_timing.hpp` and
  any overlay docs in the same PR.
- Metal is excluded from clang-format; match surrounding hand-formatting in
  `metal_render_impl.cpp`.
- Timing-only change: rendering must stay byte-identical with timers on or off;
  the timers-off path must keep its one-bool-check cost.
- GL-backend build/verify needs a Linux/Windows host (this pane is macOS/Metal
  only); if authored on macOS, the GL side rides as owed cross-host smoke.
