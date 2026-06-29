# Plan — #2092: SDF floor self-shadow acne (off-cardinal yaw) — smooth-yaw depth-reconstruction precision

**Status:** design-blocked (carrier sub-decision open — see §5).
**Class:** fable. **PR:** #2095. **Repro host:** macOS / Metal (also GL).

## 1. Architect decision (2026-06-29)

Root-fix the **smooth-yaw depth reconstruction precision** so a flat receiver
reconstructs flat in sun-space. **Not** the residualYaw-keyed receiver-tolerance
shortcut (a tolerance sized for an 8–24-voxel self-spread would overlap and
weaken genuine contact shadows and leaves the precision defect feeding every
smooth-yaw sun-shadow consumer). Follow the #1457 precedent (continuous depth
key collapses quantization ties to measure-zero; collapses to the integer path
at cardinals).

## 2. Grounded mechanism (verified in code)

- SDF smooth-yaw store: `c_shapes_to_trixel.glsl:945`
  `baseDepth = roundHalfUp(yawedIsoDistance(worldSurface, visualYaw))` — the
  continuous camera-forward iso depth **quantized to an integer**.
- Encode: `c_shapes_to_trixel.glsl:1023` →
  `encodeDepthWithFace(baseDepth, face) = baseDepth*4 + face` (`ir_iso_common.glsl:123`).
  Stored in the R32I `triangleCanvasDistances` (bits [31:2] = depth, [1:0] = face).
- Recover: the 3 smooth consumers read `rawDepth = encoded >> 2` then call
  `trixelCanvasPixelToWorld3DSmoothYaw(pixel, rawDepth, …)`
  (`ir_iso_common.glsl:584`):
  `viewPos = isoPixelToPos3D(isoRel, float(rawDepth)); return rotateYawZInv(viewPos, visualYaw)`.
- `isoPixelToPos3D` (`ir_iso_common.glsl:47`) maps the **integer** `rawDepth`
  with ∂x/∂depth = 1/3 etc.; `rotateYawZInv` then mixes the depth-quantization
  lattice into world XYZ at off-cardinal yaw, so a flat plate reconstructs as a
  staircase ⇒ a "ramp in sun-space" ⇒ within-sun-texel sunZ spread blows up to
  8–24 voxels ⇒ far-edge self-occludes against the same texel's front-edge
  sample ⇒ the acne. Bake + receive share the recovery, so they agree per-pixel
  yet both inherit the inflated spread. (Architect's diagnosis confirmed.)

`trixelCanvasPixelToWorld3DSmoothYaw` is called **only** on the `residualYaw != 0`
branch in all 3 consumers — the cardinal pose (`residualYaw == 0`) uses
`trixelCanvasPixelToWorld3D` (integer, untouched). That is the clean lever for
**cardinal byte-identity**: key the fractional encoding to `residualYaw != 0`.

## 3. Intended fix

Carry sub-integer view-frame depth on the smooth path only:

- Store (`c_shapes_to_trixel.glsl:945`, residualYaw != 0):
  `baseDepth = roundHalfUp(yawedIsoDistance(worldSurface, visualYaw) * S)` for a
  fixed-point scale `S = 2^F` (F≈3–4; ≥8 sub-steps kills the staircase).
  Keep the integer store at `residualYaw == 0` (byte-identical).
- Recover (`trixelCanvasPixelToWorld3DSmoothYaw`): reconstruct with
  `depth = float(rawDepthFixed) / S`. Safe to bake the `/S` into the function —
  it is only ever called on the smooth path (bake:116, compute:89, lighting:254).
- The encode `baseDepth*4 + face` is unchanged in form; ordering is preserved
  (`atomicMin` monotonic in the scaled depth, face still the low tiebreak).

## 4. Complete change set (from full blast-radius map)

GLSL + Metal twins each (Metal under `engine/render/src/shaders/metal/`):

| Site | File:line | Change |
|---|---|---|
| SDF smooth store | `c_shapes_to_trixel.glsl:945` / `.metal:~1106` | residualYaw-keyed `*S` |
| Recovery fn | `ir_iso_common.glsl:584` / `.metal:553` | decode `/S` (smooth-only caller) |
| Sun-shadow bake | `c_bake_sun_shadow_map.glsl:96,116` / `.metal:78` | rawDepth extraction unchanged; recovery via fn |
| Compute sun-shadow | `c_compute_sun_shadow.glsl:62,89` / `.metal:40` | same |
| Lighting | `c_lighting_to_trixel.glsl:142,254` / `.metal:99` | same |
| **Framebuffer co-sort** | `f_trixel_to_framebuffer.glsl:73-116` / `metal/trixel_to_framebuffer.metal:113` | **see §5 — the open problem** |

Untouched (cardinal / per-axis / unrelated producers verified separate):
voxel stage 1/2, particles, per-axis & world-placed resolves, AO, fog,
trixel-to-trixel, v_peraxis_scatter, Hi-Z. CPU mirrors (`IRMath::isoPixelToPos3D`
303, `pos3DtoDistanceYawed` 1350) are not on this GPU recovery path; no CPU twin
of `trixelCanvasPixelToWorld3DSmoothYaw` and no CPU↔GPU round-trip test exists.
depth-probe (`depth_probe.hpp:130`) decodes the **post-normalize** framebuffer
value (`face = encRel%4; iso=(encRel-face)/4`) — affected iff the framebuffer
co-sort scale changes (see §5).

## 5. OPEN DESIGN SUB-DECISION (why this is design-blocked)

The `triangleCanvasDistances` value is **not** consumed only by the sun-shadow
recovery. The same R32I value is the **shared carrier** for the #1884/#1958/#1960
framebuffer priority-tier composite co-sort:

`f_trixel_to_framebuffer.glsl:73-116`:
`base = round(rawDist * depthScale)` (depthScale = `effectiveSubdivisionsForHover.y`,
**1.0 for the main canvas** — the byte-identical path) → `enc = base + distanceOffset`
or the foreground-tier partition (`kMinTriangleDistance` + `kDepthForegroundBandWidth`
tier arithmetic) → `gl_FragDepth = normalizeDistance(enc)`. This pass reads the SDF
depth **raw** and does **not** branch on `residualYaw`. The SDF main-canvas depth
co-sorts against the per-axis voxel scatter (`f_peraxis_scatter`) at the `×4`
`yawedIsoDistance` scale that the tier-partition constants assume.

Scaling the stored value by `S` to carry the fraction therefore perturbs the
priority-tier co-sort (the architect's own recent #1958 Bug-A / #1960 per-trixel
work): `base`, the `kDepthForegroundBandWidth` tier centers, and `normalizeDistance`
all assume the `×4` integer scale. And the GPU **binding budget is full**
(`engine/render/CLAUDE.md` gotcha) — so unlike #1457's *separate* continuous key
(`scatterCompositeDepthKey`), there is **no free slot** to carry the fraction in a
side channel; it must ride inside the shared composite value.

Candidate carriers (architect to choose):

- **(A) Scale shared value `×S`, de-scale at the framebuffer.** Plumb `residualYaw`
  into `f_trixel_to_framebuffer` (+ its UBO C++ filler `system_trixel_to_framebuffer.hpp`)
  and divide `base` by `S` on the smooth path before the tier partition. Touches the
  #1958/#1960 co-sort pass directly; depth-probe decode must match. Most direct;
  highest risk to the recently-redesigned priority-tier path.
- **(B) Overload the existing `depthScale` (`effectiveSubdivisionsForHover.y`)** to
  fold a residualYaw-keyed `1/S` for the main canvas. Reuses #1624 plumbing but
  semantically misuses the "effSub/cubeSub" field and collides with world-placed
  detached canvases that already set it.
- **(C) Different fraction budget that keeps `base` numerically within the existing
  `×4` tier arithmetic** (e.g. spend only the unused face code 3 / sub-LSB slack) —
  likely too few bits to clear an 8–24-voxel spread; needs the architect's read on
  acceptable precision vs. co-sort invariants.

**The architect owns this call** because it trades sun-shadow recovery precision
against the #1884/#1958/#1960 composite co-sort invariants (their design). Picking a
carrier unilaterally risks a silent #1958/#1960 regression. Re-escalating per the
architect's explicit instruction ("if the continuous-depth recovery proves
infeasible without an unacceptable blast radius, re-escalate rather than silently
falling back to the receiver-tolerance hack").

## 6. Verification matrix (once carrier is chosen)

- **Cardinal byte-identity** (`residualYaw == 0`): `img_diff` shape_debug + canvas_stress
  cardinal-yaw shots = 0 drift.
- **Acne gone**: `IRCanvasStress --only floor --no-auto-rotate --no-spin
  --debug-overlay shadow --auto-screenshot 5` → shot 5 `so3_offsnap_wide` (yaw π/6)
  clean; `render-shadow-metric.py` hole/component metrics.
- **Genuine caster unchanged**: `--only maingrid,floor` shadow unchanged.
- **Co-sort not regressed** (the §5 risk): `--depth-probe` / `--depth-probe-assert`
  on the floor + a foreground solid across the off-cardinal sweep; confirm no new
  wrong-winner vs #1958/#1960.
- **Jitter**: #1922 harness (`render-jitter-metric.py` / `jitter_probe`) on the
  off-cardinal sweep.
- **Both backends**: GLSL + Metal twins; cross-host smoke label for the lagging host.
