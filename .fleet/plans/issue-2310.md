# Plan: render — light-volume continuity (#2310)

- **Issue:** #2310
- **Model:** opus (architect; planned + implemented in the 2026-07-08 design
  session with the human)
- **Status:** shipped in the same session as the plan; this file rides the
  implementation PR per the standard plan-file convention.

## Root causes fixed

1. **Camera-anchor sign bug** (`camera_anchor.hpp`): the anchor inverted the
   RAW camera pan, but the pan that centers world `P` is `−iso(P)` — the
   light volume (and 256³ occlusion grid, same helper) anchored at the
   MIRROR of the viewed position, sliding away from the view at 2× pan rate.
2. **Hard ±72 window cull + 1/8-step fade band** in `gatherLightSources` —
   the off/on/banding artifact, plus seed-position clamping that detached
   the glow from its source.
3. **Unstable falloff**: `stepFalloff` derived from the gathered subset's max
   radius — any light crossing the window boundary jumped every light's
   falloff curve.
4. **NEAREST volume sampling** — hard Manhattan diamond shells.

## Approach (as shipped)

1. Anchor = inverse-iso of the **negated** pan
   (`x=(pan.x+pan.y)/2, y=(pan.y−pan.x)/2`); raw-pan (not effective) intent
   retained, only the sign corrected.
2. Two-pass gather: pass 1 takes `maxRadius` over ALL eligible lights
   (camera-independent `stepFalloff`); pass 2 seeds each light at its
   per-axis-clamped window cell with
   `seedAlpha = 1 − manhattan(origin, clamped) · stepFalloff`, skipping
   lights whose residual cannot reach the window. The ±72 cull, the fade
   band, `warnedOOBOrigins`, `packOriginKey`, `kLightVolumeOverflowMargin`,
   and `isOriginInVolume` are deleted.
3. `GPULightSource.coneAndPad_` → `coneAndSeedAlpha_` (y = seed residual);
   both seed shaders (GLSL + Metal) write `vec4(emit, seedAlpha)`.
4. `C_CanvasLightVolume` textures NEAREST → LINEAR (consumer sampler only;
   seed/propagate are image ops and unaffected).
5. `GpuStageTiming.lightsSeeded_/lightsEligible_` + perf HUD CULL row
   `LIGHTS seeded/eligible`.
6. Lighting demo family: `--light-boundary-sweep` camera-anchor walk
   (evidence shots; future light-verify input).

## Exactness argument (boundary seeding is not an approximation)

Propagation decrements alpha per **Manhattan** step. With per-axis clamping
`N = clamp(O)`, every in-window cell `C` satisfies
`manhattan(O,C) = manhattan(O,N) + manhattan(N,C)` (each axis splits exactly
because `C_i` lies on the clamped side of `N_i`). A seed at `N` carrying
`1 − manhattan(O,N)·step` therefore reaches `C` at
`1 − manhattan(O,C)·step` — identical to an unbounded volume seeded at `O`.
Occluders outside the window are unknowable in both formulations.

## Verification (evidence in `docs/pr-screenshots/claude/2310-light-volume-continuity/`)

- Emissive boundary sweep d000→d110: continuous monotone fade; no pop, no
  1/8 banding, no glow detachment; dark exactly when residual ≤ 0.
- IRLightingSdfBlocker render-verify: 3/5 shots byte-identical, 2 within the
  intentional LINEAR-smoothing class; LOS wall shadow band intact; macOS
  references refreshed.
- shape_debug img_diff vs pre-change master: drift confined to
  light-volume-lit regions; geometry and sun-shadow pixels clean.

## Follow-ups (tracked on the lighting-domain epic, not here)

Spot cone shaping via a winning-light ID channel; per-light falloff curves;
light/shadow domain validation harness (freeze + minimap + light-verify);
zoom-vs-window coverage limitation (±64 window at far zoom).
