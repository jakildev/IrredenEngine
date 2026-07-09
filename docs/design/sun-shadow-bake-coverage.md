# Sun-shadow bake coverage (the moth-eaten cast-shadow model)

Engine-level invariant for `BAKE_SUN_SHADOW_MAP`
(`system_bake_sun_shadow_map.hpp`,
`c_bake_sun_shadow_map.{glsl,metal}`). This is the third PR on this
surface (#1784 → #2204 → #2270); the coverage model below is the settled
account of *why* the screen-space sun-shadow bake leaves holes and *what*
fills them. Future bake work must not re-derive it — in particular it must
not re-attempt a density-ratio or per-pixel-neighbour derivation, both of
which are refuted here with measurements.

## The defect

At a near-overhead sun, a cast shadow on the floor breaks into a **point
scatter of tiny disconnected shadow fragments** ("moth-eaten" / dashed —
epic #1717 items 3-4). The structural metric (`scripts/render-shadow-metric.py`
over `canvas_stress --debug-overlay shadow`, shot `shadow_overlay_floor`,
ROI `1010,540,450,250`) reads it as a high `hole_ratio` and an exploded
`components` count. On the affected host (macOS / Metal, 2× backing scale)
the master anchor is **hole_ratio 0.9475 / 58 components / largest_frac
0.306**.

## Why the bake leaves holes — the point-scatter model

The bake is **screen-space**: `BAKE_SUN_SHADOW_MAP` only sees
camera-rastered `trixelDistances` pixels. Each such pixel is projected to
its sun texel and `atomicMin`s its depth into the sun map. A cast shadow is
correct only where the sun texels **along the occluder's silhouette** each
hold a near-enough occluder depth for the floor receiver's window
(`ir_sun_shadow_sample`: `kMaxShadowDepthRange = 24` voxels, plus the PCF
2×2 kernel + bias).

The holes are the sun texels the sparse camera-rastered caster set leaves
**uncovered**: a caster face grazing the near-overhead sun, or an
axis-aligned pillar whose sun-side vertical faces are camera-hidden,
projects only a *scatter of points* into sun-UV rather than a continuous
occluding surface. Between those points the floor receiver finds either an
empty texel or only its own (self-depth-rejected) write, and reads **lit** —
a hole.

### Measured classification (#2270, this PR)

Two staged-shader receiver measurements (edit the runtime-compiled
`ir_sun_shadow_sample.metal`, no rebuild) classify the holes decisively,
ruling out a projection/reconstruction defect:

| test (receiver `ir_sun_shadow_sample`) | hole_ratio | reading |
|---|---|---|
| baseline | 0.9475 | defect reproduces |
| empty sun-texel tap → shadow (keep depth check) | 0.486 | ~half the holes are **empty** texels |
| any written tap → shadow (ignore depth) | 0.071 / 1 comp | casters are **not** in wrong texels — no projection defect |

Together: the holes are **missing coverage** (empty texels + texels lacking
a near-enough occluder), not a projection defect. Crucially, the holes are a
**2-D point scatter**, not a 1-D silhouette line (see the fix trials below).

## Refuted derivations — do not re-attempt

1. **Constant-depth density ratio (#2204 premise).** A splat radius derived
   from `srcWorldStep / texelSize` predicts ~1.3–2.1 texels, but the defect
   needs ~8–12. The density ratio is a *constant-depth* property and is
   structurally blind to grazing-surface undersampling — wrong about
   magnitude by ~10×. (#2204 measured; #2270 Phase 1 re-confirmed post-#2275.)

2. **Per-pixel oriented footprint splat (#2270 lever (a)).** Walking sun
   texels between a base caster pixel and its same-surface right/down
   neighbours can never manufacture more coverage than the adjacent-pixel
   gap — which **is the density ratio by construction** (~2 texels,
   measured: neighbour offset (0,0) self-test → exact baseline; box radius =
   computed neighbour gap → 147 fragments = radius-2 boxes). The ~8–12
   coverage only appears across depth discontinuities the same-surface guard
   must reject (admitting them → silhouette smear, 128-component garbage). So
   the neighbour walk reproduces the refuted density ratio. Byte-identical to
   baseline (0.9475 / 58) — no effect.

3. **Bounded down-ray vertical extrusion (#2270 Step 2, architect-preferred).**
   Extrude each caster's depth down the world-vertical (synthesizing the
   camera-invisible sun-side surface), `atomicMin` with an early-out when a
   texel already holds nearer geometry. **Implemented in both backends and
   measured — misses the anchor** (unconditional, L=40, no early-out:
   0.877 / 44 / 0.67; with early-out: 0.93). It fills the *dense centre* of
   the shadow but leaves the **2-D-scattered periphery fragments** — because
   the holes are a 2-D point scatter, not a 1-D image of a vertical surface
   along one sun-UV axis. A 1-D directional walk cannot bridge gaps
   perpendicular to the ray. This refutes the "the gaps lie along the same
   axis" assumption.

## The fix — bounded uniform coverage splat (`atomicMin` box)

`c_bake_sun_shadow_map` `atomicMin`s each cardinal single-canvas caster's
depth into a **(2·r+1)² box** of sun texels around its own texel
(`bakeCascadeBox`), for both cascades. This is a **uniform** splat — the one
lever that reaches the 2-D-scattered coverage the directional derivations
provably cannot.

- **`atomicMin` is load-bearing for byte-identity.** The box writes the
  caster's *own* (nearer-or-equal) depth; where a texel already holds nearer
  real geometry the write is a no-op. So a host whose bake is already dense
  ("saturated") sees no change, and the fill concentrates on the
  genuinely-empty hole texels. Saturated-host byte-identity is therefore
  **empirical, not structural** (the #2204 host — even forced r12 was
  byte-identical there — says this holds).
- **Kill switch.** `FrameDataSun.sunSplatMaxTexels_` = 0 forces the exact
  single-write path (`radius == 0` → the box collapses to one write,
  byte-identical to pre-#2270 master). If drift ever appears on a saturated
  host, this is the backstop; gate the splat on the derived density ratio
  before reaching for it.
- **The shader gate is a decode-path predicate; the C++ driver disambiguates
  the two resolve dispatches.** The gate
  (`perAxisRoute == 0 && residualYaw == 0 && sunSplatMaxTexels > 0`) is a
  **decode-path** predicate, not a camera-cardinality one. The raw smooth-yaw
  single-canvas content (`residualYaw != 0`) and the per-axis face-local store
  (`perAxisRoute != 0`) skip it by that gate, but the two **CARDINAL-layout
  resolve** bake dispatches — per-axis screen-depth (#1435) and world-placed
  cast (P4b-3, #1596) — deliberately zero `residualYaw` to reuse the cardinal
  recovery, so the gate **alone** would engage the splat on both. The two are
  **not** the same case, so `BAKE_SUN_SHADOW_MAP` drives them differently via
  `sunSplatMaxTexels_`:
  - **Per-axis resolve → splat OFF.** It zeroes the radius around this dispatch
    (`patchSunSplatRadius` + `SunSplatRestoreGuard`, alongside the existing
    `FrameYawRestoreGuard`), because the dispatch runs **only while rotating**
    and its content is footprint-dense (#1724) — the splat would be a no-op
    anyway. Gating it off makes invariant #1's per-axis / smooth-yaw
    byte-identity **structural** (radius 0 = pre-#2270 master), not a fragile
    lean on the density assumption. The `yaw30` / `yaw45` acceptance is unchanged.
  - **World-placed resolve → splat ON (intentional).** The re-voxelize cube's
    cast (#1596) is itself a screen-space projection with the **same**
    grazing-surface undersampling as the main canvas, so its resolve texture
    bakes into a moth-eaten sun-UV point scatter that the splat must fill. This
    is **measured**, not assumed: on `shadow_overlay_floor` (cardinal, frozen),
    gating the world-placed splat off shatters the cube's cast from **1 → 10
    connected components**. So the splat rides the world-placed resolve at every
    yaw — a deliberate coverage fix (same defect class as the main canvas), not
    part of invariant #1's per-axis / smooth-yaw byte-identity, and covered by
    the `shadow_overlay_floor` regression gate below.

### The #2204 cost rule and the chosen radius

The #2204 ruling rejects **speculative** cost — coverage must be bought by
*measured* need, not headroom. The uniform box pays `(2·r+1)²` `atomicMin`
per cardinal caster pixel per cascade regardless of local need, which is
exactly the cost #2204 warned about; the architect **explicitly waived** the
objection for the uniform box once the per-pixel lever (b) was proven unable
to localise the fix, on the measured anchors. The radius is chosen at the
**measured minimum**, not headroom:

| radius | hole_ratio | components | largest_frac | pass (≤8 comp, ≥0.9 frac) |
|---|---|---|---|---|
| 8 | 0.2026 | 6 | 0.9938 | ✓ (architect anchor) |
| 6 | 0.2023 | **1** | **1.0** | ✓ (**chosen**) |
| 5 | 0.1972 | 1 | 1.0 | ✓ |
| 4 | 0.2101 | 5 | 0.9995 | ✓ (pass floor) |
| 3 | 0.2588 | 119 | 0.9795 | ✗ (shatters) |
| 2 | 0.5674 | 460 | 0.44 | ✗ |

**r = 6** is the chosen value: cleanest result (single connected component),
a solid margin above the r3 cliff, and ~41 % fewer atomics than the
architect-sanctioned r8 — the smallest bounded radius that both clears the
anchor with margin and honours the #2204 cost principle. A future
density-gated firing (splat only where the local neighbourhood is sparse)
could reclaim the remaining cost; deferred (lever (a)'s localisation attempt
shows sparse-detection is subtle).

## Byte-identity regimes (the two invariants)

1. **Per-axis / smooth-yaw byte-identity (structural).** The raw smooth-yaw and
   per-axis face-local inputs skip the splat by the shader gate; the per-axis
   **resolve** dispatch — whose spoofed `residualYaw == 0` would otherwise trip
   the gate mid-rotation — has `sunSplatMaxTexels_` zeroed around it by the C++
   driver (`patchSunSplatRadius`). So every per-axis / smooth-yaw path takes
   `radius == 0` → the single write, identical to pre-#2270 master, **by
   construction** rather than by relying on the decode-path predicate to track
   camera cardinality. (The world-placed cast resolve is deliberately **not**
   part of this regime — see the "splat ON (intentional)" bullet above; it is a
   separate feature whose cast the splat is meant to cover.)
2. **Saturated-host byte-identity (empirical).** Where the bake is already
   dense, every box `atomicMin` is a no-op (farther-or-equal depth). Kept as
   an empirical property + the `sunSplatMaxTexels_ == 0` kill switch, not a
   structural guarantee.

## Acceptance / regression oracle

- `canvas_stress --debug-overlay shadow --no-auto-rotate --no-spin`, shot
  `shadow_overlay_floor`, `render-shadow-metric.py --roi 1010,540,450,250`:
  components ≤ 8 and largest_frac ≥ 0.9 (measured 1 / 1.0 at r6). This is wired
  as an automated `structural` gate in
  `creations/demos/canvas_stress/test/references/manifest.json` (the
  `shadow_overlay_floor` `extra_runs` entry, mirroring the #2092
  `floor_selfshadow` guard), so the coverage fix stays regression-guarded across
  future bake churn — backend-agnostic, one threshold for both hosts.
- `IRVoxelYaw` `zoom4_yaw0` renders a solid cast shadow.
- Per-axis `yaw30` / `yaw45` byte-identity (splat gated off).

See also `engine/render/CLAUDE.md` § "Sun shadow bake AABB sweep" and
§ "Lighting culling invariants" for the AABB-sweep coverage of off-screen
casters, which is orthogonal to this in-map coverage model.
