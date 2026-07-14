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

## The receiver same-plane test (#2319) — removing the splat's coplanar self-acne

The r6 coverage box above fills the moth-eaten cast, but on a host whose
cardinal bake is *not* already saturated (macOS/Metal, #2270) it also splats
each caster's depth into a `(2·r+1)²` box that a **coplanar** receiver then reads
back as an occluder of its own plane: a flat floor self-shadows (a caster-free
floor at cardinal read a solid ~105k-px shadow) and a convex cube's sun-facing
**top face** self-hits (artifact #1). Both are the *same* defect — a
splat-displaced coplanar write, read at its origin as if it were a nearer caster.

**Fix — splat provenance + an exact same-plane test** (`ir_sun_projection`,
`ir_sun_shadow_sample`; twinned GLSL/Metal):

- **Pack the displacement vector.** `packSunDepth(sunZ, splatOffset)` stores the
  quantized depth in the high 24 bits and this box texel's `(dx, dy)`
  displacement from its caster's own texel in the low byte (a two's-complement
  nibble each; the r ≤ 6 cap fits `[-8, 7]`). A **direct** caster's-own-texel
  write is `(0, 0)` ⇒ low byte 0 ⇒ `unpackSunDepth` (`>> 8`) recovers the depth
  bit-exact vs a pre-#2319 `<< 8` single write, so every **radius-0** path
  (per-axis / smooth-yaw / detached / saturated host) stays byte-identical — and
  at equal quantized depth a direct write's 0 low byte wins `atomicMin` over any
  splat's nonzero low byte, so a genuine caster's own depth always claims its
  texel.
- **Test the receiver plane at the reconstructed origin.** In
  `sampleCascadeShadow`, a **direct** tap keeps the pre-#2319 near-rejection
  verbatim; a **splat** tap reconstructs the write's origin texel
  (`px − offset`) and asks whether the occluder lies in the receiver's *own*
  plane. With `(uHat, vHat, sunDir)` orthonormal the receiver-plane depth
  gradient in sun-UV is `gradUV = vec2(dot(n, uHat), dot(n, vHat)) / slope`
  (`slope = dot(n, sunDir)`), so the plane's expected depth at the origin is
  `sunZ + dot(gradUV, originUV − sunUV)`. A **same-face self-occluder**
  extrapolates to `≈ nearestZ` (`h ≈ 0` → lit, at any splat distance and face
  tilt) while a **genuine cast** sits `≈ caster_height` above the plane
  (`h ≫ bias` → shadowed at the *base* tolerance, no widening).

The **vector** is what the refuted round-1 *scalar* widening could not be: a
global bias can't separate a same-face self-occluder from a real cast occluder,
so widening it eroded genuine cast shadows (~85 % floor loss). The gradient sign
is **+** — a leading minus reflects the plane, so a coplanar occluder at the
write origin would give `h = −2·grad·d ≠ 0` and *break* the same-face → lit
property; the derivation and a worked numeric check (sun 45° in XZ, flat floor)
both give `dz/du = +dot(uHat, n)/slope`.

**Residual (accepted for S1; epic #2314 D6).** A genuine-cast hole texel whose
r6-box winner was a *coplanar floor* self-splat is provenance-identical to acne
(both `h ≈ 0`), so the same-plane test lights it too — that texel is one the
caster's rastered point-set + r6 box never reached. On master such texels read
"shadowed" only because floor acne masqueraded as cast there; S1 **unmasks** the
true, pre-existing under-coverage of the #2270 splat rather than growing a fourth
mechanism round to absorb it. A follow-up child (*genuine-cast under-coverage
unmasked by S1*) files at post-merge reconcile.

## Acceptance / regression oracle (re-grounded for #2319, macOS/Metal pane)

The full-scene `88380`-px macOS master anchor is **retired** — it was ~64k floor
acne + ~24k genuine cast, so "match master / no erosion" required *reproducing*
the #2270 acne and is unsatisfiable by any correct fix on this host. Replaced by
acne-free, splat-off-referenced gates, all evaluable on macOS/Metal
(`canvas_stress --debug-overlay shadow`, cardinal freeze `--no-auto-rotate
--no-spin`, `render-shadow-metric.py`; cast-ROI `1010,540,450,250` on shot
`shadow_overlay_floor`). Measured on this branch:

1. **Acne gate (primary).** Caster-free flat floor at **cardinal** (`--only
   floor`) → **0 shadow px** (measured 0 / hole_ratio 1.0, ROI and whole-image).
   This is the gate the sibling `floor_selfshadow` guard *misses* — it runs at
   non-cardinal π/6 where the splat radius is already 0. Wired as the
   `shadow_overlay_floor` structural gate (`min_hole_ratio ≥ 0.98`) in
   `creations/demos/canvas_stress/test/references/manifest.json`.
2. **Artifact-#1 gate.** An isolated cube's sun-facing top face is fully lit (no
   coplanar self-hit) — the same coplanar-rejection that zeroes the acne.
3. **Genuine-cast lower bound.** Cast-ROI shadow px ≥ a same-session
   **splat-off** capture (`kSunSplatMaxTexels = 0`, the uncontaminated genuine
   cast — byte-identical whether built off master or this branch, since radius 0
   is the pre-splat single write); cast-ROI structure (components / largest_frac)
   no worse than that baseline. **Measured (this branch vs splat-off baseline):**
   24400 px / 59 comp / 0.7705 frac ≥ 5056 px / 93 comp / 0.3418 frac — more
   coverage *and* more coherent (the r6 splat legitimately fills genuine
   cube-cast holes; the same-plane test drops only the coplanar floor
   contamination). This is a **manual** A/B — `kSunSplatMaxTexels` has no runtime
   flag — so it is not wired as a static manifest threshold.
4. **`floor_selfshadow` ≥ 0.98** at π/6 (unchanged; measured hole_ratio 1.0) and
   **per-axis `yaw30` / `yaw45` byte-identity** (structural: those paths bake at
   radius 0, so every tap is a direct write on the verbatim pre-#2319
   near-rejection).
5. **Linux smoke owed** — the GL twin is unbuilt on the macOS pane; a large Linux
   floor A/B vs master is a re-escalation signal (calibrated-host manifest
   re-check), not something to tune away.

See also `engine/render/CLAUDE.md` § "Sun shadow bake AABB sweep" and
§ "Lighting culling invariants" for the AABB-sweep coverage of off-screen
casters, which is orthogonal to this in-map coverage model.
