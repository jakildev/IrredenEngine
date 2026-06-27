# Detached-canvas density compensation — apparent size & gather sampling are zoom × world-extent only

A detached canvas's composite (`ENTITY_CANVAS_TO_FRAMEBUFFER`) must mirror the
main world canvas's density compensation. This is an engine-level invariant that
cost three design-escalation rounds on #2043; it gets a canonical home so the
next worker on this surface doesn't re-derive it.

## The invariant

> A detached canvas's composite must mirror the main canvas's density
> compensation. **Apparent on-screen size and de-tile gather sampling are
> functions of camera zoom × world extent only — never of internal raster
> resolution** (`cubeSub` / `C_TriangleCanvasTextures::renderedSubdivisions_`).
> The main canvas enforces this via `canvasZoomLevel_ = cameraZoom / effSub`;
> `ENTITY_CANVAS_TO_FRAMEBUFFER` divides `cubeSub` out of the quad scale **and**
> the gather density the same way. Depth is independent (derived from
> `rawDist × depthScale`, not the quad scale).

## Why it holds — the two paths must agree

Both the main world canvas and a detached canvas reach the framebuffer through
the **same** de-tile gather (`f_trixel_to_framebuffer` + the parity
reconstruction `trixelFramebufferSamplePosition` in `ir_iso_common`). The gather
is density-agnostic: it works in canvas-texel units and assumes one texel ≈ one
iso-brick at whatever density the canvas was rastered. The two callers must
therefore set up the quad scale and the gather's parity anchor **consistently**
with that density, or the gather aliases.

- **Main canvas** (`system_trixel_to_framebuffer.hpp`). Rasters its pool
  at the global `effSub`. It divides density out of the quad scale —
  `canvasZoomLevel_ = cameraZoom / effSub`, folded into the model matrix — and
  multiplies the gather's camera-offset parity anchor up by the same factor —
  `cameraTrixelOffset_ *= effSub`. So one rastered world-unit maps to a fixed
  framebuffer footprint and the gather samples at ~1:1 regardless of `effSub`:
  apparent size is independent of subdivision and there are no minification
  holes.

- **Detached canvas** (`system_entity_canvas_to_framebuffer.hpp`). Rasters its
  pool at `cubeSub = renderedSubdivisions_` — which can sit **above** the global
  `effSub` when the #1570-D2 footprint cap leaves room on a generously-sized
  canvas. The composite mirrors the main canvas:
  - `densityZoom = cameraZoom / cubeSub`, used as the **quad model scale**
    (`fbRes × densityZoom × entityScale`) so apparent size becomes
    `worldExtent × zoom` — the GRID twin's size.
  - `cameraTrixelOffset_ = -entityIso × cubeSub` so the gather's
    `trixelOriginModifier` parity bit is computed in canvas-texel units.
  - `canvasZoomLevel_ = densityZoom` (the single value that plays both roles on
    the main canvas).
  - The #1883 texel-snap granularity divides by `cubeSub` too — the snap target
    is the actual (now-finer) canvas texel, not the base trixel.

  Note **only the quad SCALE** carries the divide; the placement (`entityFbCenter`
  translate) keeps full `cameraZoom` so the canvas CENTER still tracks the
  entity's world iso position. The pool is centered in its canvas, so shrinking
  the quad about `entityFbCenter` reduces apparent size without moving the solid.

Before #2043, `cubeSub > 1` on a detached canvas was effectively never exercised
(an uninitialized `C_VoxelPool::m_voxelPoolSize3D` non-deterministically pinned
the footprint cap to ≈1). Fixing that init opened the regime this invariant
governs; without the mirror, on-screen extent became
`footprint × cubeSub × fbRes × zoom / mainCanvasSize` (oversize) and the gather
minified the `cubeSub`-density tiles against a NEAREST 1:1-assuming
reconstruction (see-through gaps).

## What is NOT on this path — depth

Composite depth is **independent of the quad scale**. `enc = round(rawDist ×
depthScale) + distanceOffset` (`f_trixel_to_framebuffer.glsl`) derives from the
texture's stored `rawDist`, never the quad XY scale, and `depthScale =
effSub / cubeSub` is a separate carry (`effectiveSubdivisionsForHover_.y`).
Changing the quad scale does not perturb depth arithmetic. A world-placed
detached solid keeps depth-sorting against (and casting onto) the SDF floor at
`cubeSub > 1` — verify, don't assume, because the `cubeSub > 1` regime is newly
exercised (see below).

## Verify (the #2043 acceptance)

- **Size + solidity at `cubeSub > 1`.** `canvas_stress --only smallzoom
  --subdivisions 8 --no-spin --no-auto-rotate --auto-screenshot 8` →
  `smallzoom_low` (shot 11): the 3³ `DETACHED_REVOXELIZE` cube on a generous
  256² canvas renders **solid** (no gaps) and at the **same on-screen size** as
  the GRID twin of equal world extent.
- **Byte-identity for `cubeSub == 1` (the main invariant).** Every change in the
  composite is `× cubeSub` / `/ cubeSub`, which is identity at `cubeSub == 1`,
  so tight-canvas (orbit / canary), cardinal, screen-locked overlay, and any
  `renderedSubdivisions_ ≤ 1` canvas is **byte-identical to before the fix**
  (`img_diff` drift 0). The known non-deterministic detached-revoxelize
  round-to-cell speckle on off-cardinal shots is present on master too and is
  not a regression.
- **Depth-sort at `cubeSub > 1`.** `--only smallzoom,floor` shows the detached
  solid sitting in front of the SDF floor and casting a shadow onto it (no new
  clip, no sink-behind).
- **GL + Metal.** The change is CPU-only (no shader edit was required — mirroring
  the main canvas's density handling keeps the shared gather's parity
  reconstruction consistent), so both backends pick it up identically; still
  smoke both.

## Consumers / call-site map

- `engine/prefabs/irreden/render/systems/system_entity_canvas_to_framebuffer.hpp`
  — the composite; owns the `cubeSub` divide (this doc's subject).
- `engine/prefabs/irreden/render/systems/system_trixel_to_framebuffer.hpp` — the
  main-canvas reference (`canvasZoomLevel_ = cameraZoom / effSub`).
- `engine/render/src/shaders/f_trixel_to_framebuffer.glsl` +
  `engine/render/src/shaders/ir_iso_common.glsl`
  (`trixelFramebufferSamplePosition`) — the shared, density-agnostic de-tile
  gather both paths feed.
- `engine/prefabs/irreden/voxel/components/component_voxel_pool.hpp`
  (`m_voxelPoolSize3D`) — feeds `IRPrefab::DetachedRevoxelize::subdivisionCap`,
  which sets `cubeSub`; must be initialized or the cap (and thus this whole
  regime) is non-deterministic.

## Related docs

- [`detached-canvas-depth-default.md`](detached-canvas-depth-default.md) —
  world-placed-by-default depth participation (#1624), the depth machinery this
  invariant deliberately leaves untouched.
- [`per-axis-trixel-canvas-rotation.md`](per-axis-trixel-canvas-rotation.md) —
  the smooth-rotation forward-scatter composite (a different path to the
  framebuffer).
- `.fleet/plans/issue-2043.md` — the implementation plan + escalation history.
