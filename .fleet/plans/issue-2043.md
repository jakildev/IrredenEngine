# issue-2043 — detached entity-canvas voxel objects: gaps + uncontrollable size

**Status: RESOLVED (Option A, round 3).** Both symptoms are one root cause: the
detached composite (`ENTITY_CANVAS_TO_FRAMEBUFFER`) was missing the
density/zoom compensation the main world canvas has. Fixed by mirroring the main
canvas. The durable invariant lives in
[`docs/design/detached-canvas-density-compensation.md`](../../docs/design/detached-canvas-density-compensation.md)
— read that first; this file is the implementation/escalation record.

## Root cause (confirmed with the human)

1. **`C_VoxelPool::m_voxelPoolSize3D` was uninitialized** (latent bug, fixed
   here). Its only consumer is the #1570-D2 detached footprint cap
   (`subdivisionCap`), so the cap read garbage and **non-deterministically
   pinned `cubeSub`** — why the report was flaky and earlier passes "couldn't
   reproduce at default sub". Correct + necessary; opens the `cubeSub > 1`
   regime the composite fix governs (so the two MUST land together — #1 alone
   reliably oversizes generous-canvas detached solids).
2. **The defect: `cubeSub` (internal raster resolution) leaks into apparent size
   + de-tile gather sampling, at all zoom ≥ 1.** The main canvas divides density
   out (`canvasZoomLevel_ = cameraZoom / effSub`); the detached composite never
   got the mirror, so on-screen extent was `footprint × cubeSub × fbRes × zoom /
   mainCanvasSize` (oversize) and the gather minified `cubeSub`-density tiles
   against a NEAREST 1:1-assuming parity reconstruction (gaps).

   (Camera zoom is hard-clamped to ≥ 1 by `kTrixelCanvasZoomMin`, so this is NOT
   a low-zoom bug and Option 1 — zoom-track `cubeSub` at the raster cap site — is
   a structural no-op. The clamp stays; lifting it is a separate epic.)

## The fix — Option A (composite-side density divide), CPU-only

In `system_entity_canvas_to_framebuffer.hpp`, mirror the main canvas for
`cubeSub = renderedSubdivisions_`:
- `densityZoom = cameraZoom / cubeSub` drives the **quad model scale** →
  apparent size = `worldExtent × zoom` (GRID-matched); also set as
  `canvasZoomLevel_`.
- `cameraTrixelOffset_ = -entityIso × cubeSub` → the gather `trixelOriginModifier`
  parity bit is computed in canvas-texel units (mirror of the main canvas's
  `cameraTrixelOffset_ *= effSub`).
- The #1883 texel-snap granularity divides by `cubeSub` (snap to the now-finer
  canvas texel).

Only the quad SCALE divides; the placement (`entityFbCenter`) keeps full
`cameraZoom` so the canvas center still tracks the entity. No shader edit needed
— mirroring the main canvas keeps the shared gather's parity reconstruction
consistent. **Depth untouched** (`depthScale = effSub/cubeSub`, `distanceOffset`
unchanged; depth derives from `rawDist`, not the quad scale).

## Re-scoped acceptance (supersedes #2043's; #2046 subsumed) — status

- [x] Generous world-placed detached solid at `subdivisions > 1` (`cubeSub > 1`)
  renders **solid** + at **GRID-twin on-screen size** at zoom ≥ 1
  (`--only smallzoom --subdivisions 8`, `smallzoom_low`).
- [x] `cubeSub ≤ 1` / tight-canvas / cardinal / overlay output **byte-identical**
  to master (`img_diff` drift 0 on 8/10 default shots; the 2 off-cardinal
  detached-revoxelize shots drift only the known round-to-cell speckle that
  master-vs-master shows too).
- [x] World-placed detached solid still depth-sorts against + casts onto the SDF
  floor at `cubeSub > 1` (`--only smallzoom,floor`: cube sits on floor, casts
  shadow; no clip / sink-behind).
- [x] Render-debug-loop evidence (full-frame + ROI crop pair) on the PR.
- [ ] GL build/smoke (authored + verified on macOS/Metal; CPU-only change →
  `fleet:needs-linux-smoke`).

## Repro harness (in this PR)

`canvas_stress --only smallzoom --subdivisions 8 --no-spin --no-auto-rotate
--auto-screenshot 8` → `smallzoom_low` (shot 11) = the bug pose (a 3³
`DETACHED_REVOXELIZE` cube on a generous 256² canvas next to a GRID twin of equal
world extent). Add `,floor` for the depth-sort variant.

## Revision history

- 2026-06-27 (round 1) — two NEEDS-DESIGN; architect chose Option 1 (zoom-track
  cubeSub); #2046 closed as subsumed.
- 2026-06-27 (round 2) — implementing Option 1 surfaced the uninitialized
  `m_voxelPoolSize3D` (fixed) + the `kTrixelCanvasZoomMin` clamp making Option 1
  a no-op + the real composite-side defect. Re-escalated.
- 2026-06-27 (round 3) — architect: Option A (composite-side density divide),
  "no shader edit" constraint lifted (turned out not needed). Implemented +
  verified on macOS/Metal; durable invariant doc'd in
  `docs/design/detached-canvas-density-compensation.md`.
