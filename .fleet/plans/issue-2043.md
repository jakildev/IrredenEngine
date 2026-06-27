# issue-2043 — detached entity-canvas voxel objects: gaps + uncontrollable size

**Status: DESIGN-BLOCKED (re-escalated 2026-06-27).** The architect's Option-1
direction (zoom-track the detached raster density) is a structural no-op against
the current engine — see "Why Option 1 cannot work" below. This PR carries the
two real, verified findings + a deterministic repro, and re-escalates for the
composite-side decision.

## Two confirmed root causes

### 1. `C_VoxelPool::m_voxelPoolSize3D` was uninitialized (latent bug, FIXED here)

`C_VoxelPool(ivec3)` set only the scalar `m_voxelPoolSize` (x·y·z); the 3D field
`m_voxelPoolSize3D` was never assigned. Its only consumer is the #1570-D2 detached
footprint cap (`IRPrefab::DetachedRevoxelize::subdivisionCap` via
`system_voxel_to_trixel`), so that cap was reading garbage and
**non-deterministically pinning `cubeSub`** (observed `cubeSub=1` on this host —
which is exactly why earlier passes "couldn't reproduce at default sub"; a
different garbage read on the game host let `cubeSub>1` through, producing the
flaky original report). Fixed by initializing the field in the ctor + a default
member initializer. Verified byte-identical for the existing tight-canvas demos
(orbit/canary: footprint cap is correctly 1 there — `iso.y` footprint = 4·poolDim
dominates — matching the garbage-derived 1).

### 2. The oversize is `cubeSub` leaking into apparent size — at zoom ≥ 1

With the cap reading the correct pool size, a **generously-sized** detached canvas
(footprint cap ≫ effSub) admits `cubeSub > 1`, and the detached composite's
on-screen extent is `footprint × cubeSub × fbRes × zoom / mainCanvasSize` — the
`cubeSub` factor wrongly scales apparent size. The main world canvas divides it
back out (`canvasZoomLevel_ = cameraZoom / effSub`, `system_trixel_to_framebuffer`);
`ENTITY_CANVAS_TO_FRAMEBUFFER` has no such `/cubeSub` divide. Deterministic repro
(`--only smallzoom`, below): a 3³ DETACHED_REVOXELIZE cube renders ~8× the size of
a GRID cube of identical world extent at baseSub 8.

## Why Option 1 (zoom-track `cubeSub` at the raster cap site) cannot work

- **Camera zoom is clamped to ≥ 1.0** (`kTrixelCanvasZoomMin = {1,1}`,
  `ir_constants.hpp`). `setCameraZoom(0.5)` → 1.0. So zoom < 1 — the regime the
  whole "low zoom" fix targets — **cannot occur**. Verified: every shot, including
  the "zoom 0.32"/"0.5" ones, renders at clamped zoom 1.
- Therefore `cubeSub = clamp(min(effSub, round(baseSub × zoom)), 1, cap)` always
  has `zoom ≥ 1` ⇒ `round(baseSub × zoom) ≥ effSub` ⇒ `min(...) = effSub` ⇒
  **cubeSub unchanged**. The zoom-track is a no-op at every reachable zoom.
- Independently: the detached re-voxelize canvas's raster reads `getCameraZoom()`
  ≥ 1 by the same clamp, and the **oversize is present at zoom 1** (cubeSub factor)
  — a zoom-track can't reduce cubeSub at zoom 1 even conceptually.

## Recommendation — Option A (composite-side density divide)

Fix in `ENTITY_CANVAS_TO_FRAMEBUFFER` (which DOES run at every zoom): divide
`cubeSub` out of the gather density and the quad scale so apparent size + gather
sampling are functions of camera zoom + world extent, never internal raster
resolution — the direct mirror of the main canvas's `canvasZoomLevel_ =
cameraZoom / effSub`. This is the option the architect's FIRST NEEDS-DESIGN
identified ("no `/cubeSub` density divide the way the main canvas does `/effSub`")
and flagged for regression risk (the placement math — `entityFbCenter`, the #1883
texel-snap jitter fix, the #1944 effective-camera-iso pivot — and the
`depthScale = effSub/cubeSub` offset are expressed in the current zoom-baked
space). That risk is the design call to make: it needs the architect's sign-off
on touching the composite placement/depth math, with byte-identity guarded by
the repro harness below.

**Coupling note:** finding #1 (pool-size fix) makes the oversize *deterministic*
for generous canvases (it was flaky before). So finding #1 must land *together*
with the Option-A size fix — shipping #1 alone makes generous-canvas detached
objects reliably oversized.

## Repro harness (in this PR)

`canvas_stress` gained `--subdivisions N` + `--zoom <f>` debug flags and an opt-in
`--only smallzoom` group (one 3³ DETACHED_REVOXELIZE cube on a generous 256²
canvas next to a GRID cube of equal world extent + `smallzoom_low`/`smallzoom_high`
shots). Repro:

```
IRCanvasStress --only smallzoom --subdivisions 8 --no-spin --no-auto-rotate --auto-screenshot 8
# shot 11 (smallzoom_low): detached cube ~8× the GRID twin = the oversize bug.
```

## Revision history

- 2026-06-27 (round 1) — two NEEDS-DESIGN escalations; architect chose Option 1
  (zoom-track cubeSub), #2046 closed as subsumed.
- 2026-06-27 (round 2, this PR) — implementing Option 1 surfaced: (a) the
  uninitialized `m_voxelPoolSize3D` (fixed), (b) the `kTrixelCanvasZoomMin = 1`
  clamp making Option 1 a structural no-op, (c) the oversize is a zoom-≥-1
  cubeSub-in-size defect. Re-escalated; recommend Option A (composite-side).
