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

## Resolution (architect, 2026-06-22; revised 2026-06-23)

Design decided on the strength of this spike; #1884 was promoted to a sub-epic
and implementation split into independently-verifiable children. The agreed
architecture is a unified **quadrant-stable** depth encoding plus a **two-tier
disjoint near-plane partition** for foreground priority. Two pieces:

**Quadrant-stable iso-depth (retires #1370).** On the SDF smooth-yaw path the
stored depth metric derives its cos/sin from the **cardinal bracket**
(`cardinalYawCosSin(rasterYawCardinalIndex(rasterYaw))`) instead of the
continuous `visualYaw`, so the stored depth is piecewise-constant per 90°
quadrant and no longer drifts toward/under geometry above it near the ±45°
bracket boundary. On-screen placement still uses `visualYaw`; only the stored
depth becomes quadrant-stable. Cardinal frames are byte-identical (the smooth
branch is gated off at cardinals). This shares Bug A's iso-depth-ambiguity root.

**Disjoint near-plane priority partition (resolves Bug A — revised model).** The
original plan proposed an *additive* priority band
(`enc = priorityBand·BAND + cardinalIsoDepth·4 + face`). Implementation surfaced
that **no fixed additive band can dominate unbounded world placement**: an
additive band must out-size *twice the world's iso-depth spread* to lift a
far-placed foreground entity past the near edge of world content, and that term
scales with world extent — `canvas_stress`'s radius-200 GRID orbit exhausts it
(the band-headroom × subdivisions trap the plan flagged). Escalated
`fleet:design-blocked`; the architect ruling **replaced the additive band with a
disjoint near-plane partition**:

```
world content (priority 0):  enc = cardinalIsoDepth·4 + face                  // UNCHANGED
                             enc = max(enc, kDepthForegroundCeil + 1)         // clamped OUT of the near band
foreground priority:         enc = clamp(localIsoDepth + bandCenter,          // pinned INTO the reserved
                                         kMin, kDepthForegroundCeil)          //   near band, self-occluding
```

The most-negative `kDepthForegroundBandWidth` (16384) codes of the shared
`[kMin, kMax]` range are **reserved** exclusively for foreground-priority detached
solids. **Invariant (the point):** a foreground fragment is unconditionally nearer
than any non-priority fragment *independent of world extent* — dominance is by
**partition membership**, not by out-sizing the world's depth spread. World
content is clamped to stay out of the band; the clamp is a **no-op** for every
current demo at the effSub-16 cap (it fires only when `cardinalIsoDepth·4 <
kDepthForegroundCeil` ≈ world extent far past the r=200 orbit), so the cardinal
fast path and all in-budget content stay byte-identical. Beyond the documented
ceiling, far world content **saturates** against the boundary (loses depth
resolution) instead of letting a background fragment beat a priority solid —
strictly better than the additive model, which broke foreground dominance the
moment the world got deep. The encodable range is already symmetric about
`enc = 0`, so this is not an off-center / insufficient-buffer problem; centering
changes the offset, not the spread.

The unsatisfiable world-extent `static_assert` (`2·4·maxSubdividedIso + 3 < BAND`)
is replaced by a **partition-layout** assert on constants only
(`kDepthForegroundCeil < 0`; `kMin ≤ kDepthForegroundCeil < kMax`).

**Per-trixel generalization (#1960).** The partition generalizes to N disjoint
sub-ranges; `priority = max(entity, trixel)` selects the tier. #1958 (B) ships the
**two-tier** split only (world + one foreground tier); per-trixel tiers are D
(#1960).

**32-bit composite depth (#1983, filed).** The `±65535` ceiling is a
normalization convention on the shared composite depth, not a hardware limit.
Widening it (DEPTH32F or a manual integer R32I composite) would *raise* the
ceiling but not make it unbounded; under world streaming (#938) coordinates are
genuinely unbounded, where the partition's O(1) correctness still wins. #1983
evaluates retiring the convention and simplifying the partition special-casing
for practical worlds; the partition ships now (unconditional, unblocks B/C/D/E).

The three findings above map to the children:

| Finding | Child | Scope |
|---|---|---|
| Finding 1 (detached composite writes no depth) | **A — #1957** | detached depth-write (foundation; unblocks the encoding work) |
| Finding 2 / Bug A (iso-depth ranks floating solid behind floor) | **B — #1958** | quadrant-stable SDF depth + two-tier disjoint near-plane priority partition (blocked by A) |
| Finding 3 / Bug B (cardinal-180 `+slot` tiebreak) | **C — #1959** | per-axis cardinal-180 geometric tiebreak (blocked by B) |
| — | **D — #1960** | per-trixel priority + demos (blocked by B) |
| — | **E — #1961** | rotation perf parity (blocked by B) |

This doc is the durable evidence base for that decomposition; #1950 lands it and
closes (no implementation here — the fix ships through the children above).
