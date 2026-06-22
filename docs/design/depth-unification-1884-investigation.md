# Depth / clipping unification across render types — investigation spike (#1884)

Terminal child (Child 3) of epic #1881. Per the architect's framing this is an
**investigation spike first**: produce a repro + a unit table of what each render
type writes to the shared framebuffer depth, and the root cause of the two
"behind-face wins" crossings, *before* prescribing the fix.

All measurements below are macOS / Metal, `IRCanvasStress` and `IRPerfGrid`,
current `origin/master`, using the merged `--depth-probe X,Y` readback (#1910 /
PR #1935). The main framebuffer is 642×722; the probe reads framebuffer texture
pixels (zoom-independent — zoom only scales `FRAMEBUFFER_TO_SCREEN`).

## Depth-unit table (what each path stores in the shared framebuffer depth)

All paths target the same encoding: `gl_FragDepth = normalizeDistance(enc)` where
`normalizeDistance(d) = (d − kMin)/(kMax − kMin)`, `kMin = −65535`, `kMax = 65535`
(`ir_constants.hpp:47,51`). The probe decodes `gl_FragDepth` back to `enc` (it
calls this `rawDist`). `encodeDepthWithFace(isoDepth, face) = isoDepth*4 + face`
(`ir_iso_common.glsl:123-125`; `kDepthEncodeShift = 4`, `ir_render_types.hpp:187`).

| Render type | Stored `enc` (the probe's rawDist) | Writes depth attachment? | Source |
|---|---|---|---|
| Single-canvas gather (GRID voxels) | `isoDepth*4 + face`, `isoDepth = x+y+z` (smooth-yaw: yawed `R_z(−yaw)` iso) | **yes** | `c_voxel_to_trixel_stage_1.glsl:289,324`; `f_trixel_to_framebuffer.glsl:95` |
| SDF floor / `SHAPES_TO_TRIXEL` | `baseDepth*4 + face`; cardinal `baseDepth = surfaceD + originDistance`, smooth `baseDepth = round(dvx+dvy+z)` (same yawed iso — "SDF + voxels stay co-sorted") | **yes** (same gather) | `c_shapes_to_trixel.glsl:937-942,1016` |
| Per-axis forward scatter | `scatterCompositeDepthKey = yawedSum*4 + slot`, `yawedSum = x(c−s)+y(s+c)+z` | **yes** | `ir_iso_common.glsl:575-580`; `f_peraxis_scatter.glsl:71` |
| Detached canvas composite (`ENTITY_CANVAS_TO_FRAMEBUFFER`) | computes `round(modelRawDist*depthScale) + worldDepth*effSub*4`; world-placed offset `worldDepth = pos3DtoDistance(roundVec3HalfUp(translation))` | **NO — see Finding 1** | `system_entity_canvas_to_framebuffer.hpp:224-235`; `f_trixel_to_framebuffer.glsl:64-66` |

At the repro's `effSub = 1` every path's *scale* agrees (`isoDepth*4`). The two
crossings are therefore **not** a subdivision-scale bug. They are:

## Finding 1 — the detached composite never writes the depth attachment

The detached composite draws **color** under a depth *test* but does **not write
back** to the framebuffer depth attachment. Ground truth (`IRCanvasStress
--only … --depth-probe`, probe pixels inside the magenta canary cube, world
`z=−8`, floor world `z=4`):

```
pixel (320,337)   canary-only  : <background only>   (cube color IS drawn here)
                  floor-only   : rawDist=-38
                  canary,floor : rawDist=-38          (identical to floor-only)
pixel (313,330)   canary-only  : <background only>
                  floor-only   : rawDist=-66
                  canary,floor : rawDist=-66
```

Adding the canary changes **zero** depth values anywhere — even where its color
wins the pixel, and even with no floor present (`canary-only` is background at
the cube's own center). So:

- A detached solid can be **occluded by** the floor/world (it tests against the
  existing depth) but cannot **occlude** the floor, other detached canvases, or
  itself across instances — a half-implementation of #1624's "depth-participate".
- **#1910's probe is blind to exactly the path #1884 is about.** The probe doc
  and `engine/render/CLAUDE.md` both state "the detached-canvas composite
  (`ENTITY_CANVAS_TO_FRAMEBUFFER`) writes its winning fragment's `gl_FragDepth`
  into this one attachment." That claim is **empirically false on Metal**. The
  gather shader does write `gl_FragDepth` (`f_trixel_to_framebuffer.glsl:95`),
  but the per-instance Metal render encoder for this pass
  (`metal_render_impl.cpp`, a fresh encoder per `drawElements`) does not persist
  it. Success-criterion #1 (probe reads *each* contributing path, incl. detached)
  is not actually met by #1910.

## Finding 2 — Bug A: iso-depth convention ranks a floating solid behind the floor

Because the depth convention is iso-depth `x+y+z`, the canary cube's lower/back
faces have a larger `x+y+z` than the floor surface point projecting to the same
screen pixel, so the floor wins the per-texel test even though the cube floats
*above* the floor in world-Z. Measured floor depth along the cube's vertical
extent: top pixels `rawDist ≈ −66`, center `≈ −38`, bottom `≈ −6`. The cube's
composite depth = `worldDepth*4 (=−32) + modelRawDist`, and `modelRawDist` spans
≈ `[−60,+60]` across the 10³ cube's faces:

- cube top faces: `−32 + (−60) = −92  <  −66` → cube wins ✓
- cube bottom faces: `−32 + (+60) = +28  >  −6` → **floor wins** ✗ (the clip line)

The floor point that wins (e.g. world ≈ `(−1.75,−1.75,2)`, iso ≈ −1.5) genuinely
has a smaller `x+y+z` than the cube's far-bottom corner (world ≈ `(5,5,−3)`, iso
≈ +7) — the iso-depth test is *correct by its own convention* but *wrong by
intent* ("the cube floats above the floor, show all of it"). At low zoom the
affected band is sub-pixel; at high zoom it spreads to a visible horizontal clip
line (the architect's "fixed depth error made visible at high zoom"). Confirmed:
`--only canary,maingrid` is clean (cube vs GRID cubes sort by the same iso-depth,
consistently), `--only canary,floor` breaks.

## Finding 3 — Bug B: per-axis composite key degenerates at cardinal-180

`scatterCompositeDepthKey = [x(c−s) + y(s+c) + z]·4 + slot`. At yaw = 180°
(`c=−1, s=0`) this collapses to `(−x − y + z)·4 + slot`: the X-face and Y-face
exact cells along the shared front vertical edge produce the **same** `yawedSum`,
so only `+slot` breaks the tie — and slot 0 (X) always beats slot 1 (Y) under
GL_LESS regardless of true geometric depth. That is the ~20px `YXYX` doubled
face-stripe the architect saw at cardinal-180 (clean at 90/270). Mechanism
confirmed by static derivation against the architect's per-axis-ID overlay
finding; the `+slot` tiebreak is the wrong arbiter when `yawedSum` ties.

## Why the fix needs a design call (do not guess — `ir_render_types.hpp` shared)

1. **Detached depth-participation (Finding 1).** Should the composite *write*
   depth (completing #1624)? It is the only path that doesn't, and on Metal the
   per-draw encoder/storeAction must be made to persist depth while keeping the
   cardinal + `screenLocked_` fast paths byte-identical. This also decides
   whether #1910's probe contract gets fixed or the composite does.
2. **Iso-depth ambiguity for floating solids (Finding 2).** "Render fully in
   front of any floor they're above" requires *overriding* the `x+y+z` ordering
   for detached-vs-floor — a depth-semantics decision that, per the file's
   gotcha, "ripples across every render type" (shared `kDepthEncodeShift` /
   `kTrixel*Distance`). Options span: give a detached solid a single
   representative depth vs the floor; bias the floor; or a render-type priority
   band in the encoding.
3. **Bug B key (Finding 3).** Any change to `scatterCompositeDepthKey` rides the
   per-axis path #1883 just stabilized; the tiebreak needs an arbiter that is
   geometric (true per-face depth) rather than `slot`, without regressing the
   90/270 cardinals or the off-cardinal poses.

## Repro commands

```
# Bug A — detached canary clips behind the SDF floor:
IRCanvasStress --only canary,floor --no-spin --no-auto-rotate --auto-screenshot
IRCanvasStress --only canary,floor --no-spin --no-auto-rotate --auto-screenshot --depth-probe 320,337
#   compare against --only floor (identical depths == composite writes no depth)
# Bug B — per-axis doubled stripe at cardinal-180:
IRPerfGrid --mode dense --grid-size 64 --yaw-ramp --auto-screenshot   # step 18 (180°)
```
