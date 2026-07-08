# Plan: Metal headless GPU vehicle-A harness + R32I cross-encoder regression guard (closes #1640)

- **Issue:** #1640 (unblocks #2091 once merged)
- **Model:** opus (Metal backend headless GPU bring-up + render-invariant investigation)
- **Date:** 2026-07-08 (architect ruling folded in; supersedes the C3-vs-C1 investigation framing)

## Ruling — investigated, no defect

#1640 was framed as a Metal backend gap: a foreign-canvas R32I distance texture
read back the clear sentinel (65535) from a **second** in-tick compute dispatch,
suspected to be a cross-encoder **visibility** bug (candidate **C3**) or a
pipeline-ordering starvation (candidate **C1**). Both are now ruled out:

- **C3 is refuted by repro.** The headless vehicle-A harness this PR adds
  (`test/render/gpu_compute_dispatch_test.cpp`, Metal half) proves a texture
  written by one compute dispatch is correctly read by a second dispatch sharing
  the same command buffer across separate compute encoders — the exact structure
  C3 blamed — and the oracle control proves a genuinely missed write *would* have
  surfaced as the 65535 clear. Metal cross-encoder R32I write→read visibility is
  **sound**.
- **C1 dissolves on two static facts (no GPU frame capture needed).** (1) The
  consolidated per-canvas raster (compact / stage-1 / stage-2, now one per-canvas
  tick inside `VOXEL_TO_TRIXEL_STAGE_1`) encodes strictly before
  `RESOLVE_PER_AXIS_SCREEN_DEPTH` and `BAKE_SUN_SHADOW_MAP`, so even a direct
  foreign read at bake time is encoded after every canvas's writes. (2) The
  per-axis screen-depth resolve already does a second-in-tick R32I `READ_ONLY`
  image bind on three non-main canvases every non-cardinal frame on Metal in
  production, and works.

**Actual cause of the #1626-era symptom:** multi-canvas shared-state, not the
backend — stage-2 was a separate system clobbering shared voxel SSBOs across
multi-canvas scenes, and foreign FrameData leaked into the bake, so it sampled
the wrong texels (uniform clear sentinel). That is the exact class the per-axis
resolve now defensively patches. `resolve-then-bake` is therefore the permanent
design independent of any backend bug — already ratified as the Q2-REVISED
invariant (2026-06-09).

## Deliverable (this PR)

1. **Standing regression guard + reusable vehicle-A Metal harness.**
   `bootstrapHeadlessRenderDevice` (windowless `MTLDevice` + queue, no swapchain)
   plus two gtests in the `#elif IR_GRAPHICS_METAL` half of
   `gpu_compute_dispatch_test.cpp` and two R32I test kernels under
   `test/render/shaders/metal/`. Guards the Metal cross-encoder R32I contract and
   is the tool future headless Metal GPU tests build on.
2. **Design-doc resolution** (`docs/design/detached-revoxelize-world-light.md`,
   Q2-REVISED block + the P4b-3 note): the "Metal scratch-delivery gap tracked as
   its own backend issue" caveat is replaced with the no-backend-bug resolution.
3. **PR metadata:** `Closes #1640`; title drops WIP.
4. **Deliberately out of scope:** the `attachMetalRenderDevice` dedup (shares
   ~60% of bring-up with `MetalRenderImpl::init()`) — production init-path churn
   isn't justified by a test-only harness (architect agreed with the simplify
   call).

## Downstream

#2091's `Blocked by: #1640` clears automatically on merge. Its investigation
starts from "visibility is sound — look at frame-data / coordinate / binding
state in the world-placed resolve path, with the per-axis resolve as the working
reference" (architect is separately correcting #2091's falsified root-cause
paragraph).

## Acceptance criteria

- `IrredenEngineTest --gtest_filter='*GpuComputeDispatch*'` → Metal gtests green
  on macOS; full suite green (no regressions).
- Design doc no longer references a Metal backend bug / scratch-delivery gap for
  #1640; the resolution is recorded in the source-of-truth Q2-REVISED block.
- PR closes #1640.
