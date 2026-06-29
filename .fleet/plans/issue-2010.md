# Plan: render — re-voxelize SOLID staircase venetian banding (sun-shadow riser facet)

- **Issue:** #2010 (`fleet:task`, part of epic #1717 staircase-banding family)
- **Model:** opus
- **Date:** 2026-06-27 (spike); resolved 2026-06-29 (opus-architect)
- **Architect:** opus-architect

## Status: design RESOLVED — marker-free, mirror #2089

The confirmation spike (committed evidence on the branch) **refuted** the
original mask/geometry premise: re-voxelize coverage is hole-free and the
exposed mask is correct by construction. The banding is **lighting**. After
steward re-scoping, #2010 owns the **sun-shadow riser facet**; #1718 (PR #2089,
**merged** 2026-06-29) owned the **AO facet**. Disjoint shaders, no collision.

The 3 design-block cycles parked on a premise — "the sun-shadow tolerance needs
a shared per-canvas 'rebuilt-rotating GRID solid' marker that #1718 must plumb
first." **That premise is dissolved:** #2089 shipped **marker-free** (touches
only `c_compute_voxel_ao.glsl` / `.metal` — no UBO field, no canvas component,
no C++). There is no marker to wait for or reuse.

## Approach (resolved): geometric staircase discriminator (same-face step)

Same marker-free intent as #2089 (discriminate the self-step **purely in-shader,
from rendered geometry** — no per-canvas marker, no UBO change, no C++), but the
discriminator is reframed for the sun-shadow receiver. #2089's literal probe
(a DIFFERENT-face occluder ~1 cell in front + a `pixel + 2*delta` same-face
return) is tuned for the AO receiver — a *tread* whose occluder is a *riser*. The
sun-shadow shadowed pixels are the staircase risers/treads themselves, for which
that probe matches almost nothing (empirically ~3% of banded pixels).

The reliable signal at the sun-shadow receiver is the **same-face round-to-cell
step**: a tilted-flat surface quantized into a voxel staircase has a SAME-face
in-plane neighbour offset ~1 cell along the receiver's outward normal, whereas a
flat cardinal face is coplanar (offset ~0) and a genuine concave crease meets a
DIFFERENT face. So a same-face neighbour at `[0.5, 1.5]` along the normal (8
in-plane directions, axis + diagonal) is the unambiguous staircase signature.
When detected, lift the near sun-shadow rejection so the receiver's own ~1-cell
in-cell step blocker is skipped; genuine FAR contacts (and all flat receivers)
are untouched.

## Files (sun-shadow shaders only)

- `ir_sun_shadow_sample.glsl` / `.metal` — `sampleCascadeShadow` gains a
  `selfStepDepthRange` arg that lifts the NEAR rejection
  (`nearReject = max(bias, selfStepDepthRange)`); the `kMaxShadowDepthRange`
  window keeps using `bias`. `worldSunShadowFactor` threads it through (GLSL: a
  4-arg overload + 3-arg delegate; Metal: a defaulted param). 0 = pre-#2010
  byte-identical (detached world-receive + all flat geometry).
- `c_compute_sun_shadow.glsl` / `.metal` — `detectSelfStepStaircase()` probes 8
  in-plane neighbours for a same-face ~1-cell step. main() recomputes the factor
  with `kSelfStepDepthRange` only when a SHADOWED receiver (`factor < 1.0`) on the
  single-canvas static-camera path (`!perAxis && residualYaw == 0`) is detected
  as a staircase — lit pixels and flats skip the probe (byte-identical).

## Validation (macOS / Metal)

- canvas_stress `--only gridspin --debug-overlay shadow --no-auto-rotate`: the
  dense magenta venetian rows on the GRID spin cubes clear; a residual genuine
  edge shadow (vertical column) is preserved.
- canvas_stress full scene: `img_diff` (fix vs master) drift is confined to the
  GRID self-shadow — the SDF floor and every detached canvas are untouched (no
  genuine-shadow over-suppression).
- shape_debug labeled crops (incl. cardinal yaw180): `img_diff` drift = 0
  (byte-identical static cardinal scenes).
- Pre-existing blocker found + filed: canvas_stress (and other migrated demos)
  reject their own custom CLI flags because `IREngine::init(argc, argv)` parses
  strictly without the flags registered (IRArgs migration gap).

## Scope / acceptance (steward GRID-only narrowing)

- **GRID main-canvas spin cubes only.** Detached riser banding is routed to
  #1444's smooth deform — not patched here.
- `--debug-overlay shadow` over the GRID spin cubes: no magenta riser venetian
  rows at any orientation; genuine contact shadows preserved.
- `#1922` jitter harness passes; both backends; `--no-lighting` byte-clean;
  cardinal static scenes byte-identical (img_diff drift 0).
- Size `kSelfStepDepthRange` against a post-#2089 baseline (merged): once AO
  stops darkening the treads, the magenta residual to tune against is cleaner.

## Reconciliation

Sun-shadow riser facet of the #1717 staircase-banding family. Sibling #1718
(AO facet, PR #2089, merged). Detached riser banding → #1444. The shared
`ir_sun_shadow_sample` lookup stays byte-identical for the detached world-receive
path (#1576 P4b-2) via the 0-default range.
