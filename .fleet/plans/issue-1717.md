# Plan: render — shadow & lighting quality epic (consolidate the bake with the scatter face-deformation math)

- **Issue:** #1717 (umbrella, `fleet:epic`, `human:approved`)
- **Model:** opus (all consolidation children)
- **Date:** 2026-06-27
- **Architect:** opus-architect

## Scope

#1717 inventoried five shadow/lighting symptoms (step self-shadow banding,
yaw-frozen cast shadows, interior gaps, ragged silhouettes, wrong-face hits).
The 2026-06-27 user direction sharpens the original "consolidation note": the
color face-raster path was already fixed under yaw with a consolidated
deformation library, but the **sun-shadow bake is a separate code path that
reuses none of it**. The fix is to route the bake (and, where it helps, the
lighting face-darkening normal/AO selection) through the SAME primitives the
scatter color path uses, so "which face we project to" (sun basis vs camera
iso) is the only per-stage delta.

## The seam (verified by code read, 2026-06-27)

Shared, mature primitives the COLOR path uses — and the bake does NOT:
- `faceDeformationMatrix` / `deformedTrixelIsoPixel` — ir_iso_common.glsl:680-707; CPU mirror ir_math.hpp:720-879
- `visibleTriplet` (visible-face selection), `yawedIsoDistance` (continuous-yaw depth) — ir_iso_common
- analytic edge-aware coverage `scatterAnalyticEdgeCoverage` (#1933 / #2013, commit 49fcadff) — currently inlined in f_peraxis_scatter

The bake path (c_bake_sun_shadow_map.glsl) instead:
- reprojects already-rasterized screen pixels with its own `-dot(pos3D, sunDir)`
  (:128-129) — no residual-yaw deform → item 2 (yaw-frozen), item 4 (ragged);
- writes one atomicMin per pixel, no analytic coverage (:74-81) → item 3
  (interior gaps), #1784 (swiss-cheese floor);
- silently clips outside the 1024^2 cascade (:73-81) + 40% split
  (system_bake_sun_shadow_map.hpp ~L536) → item 5 (wrong-face / missing faces).

## Children (serialized — they share c_bake_sun_shadow_map + ir_iso_common; one at a time to avoid conflicts)

### C0 — #2080 — Detached-canvas world-lighting participation [opus, parallel-safe]
Detached canvases are gated, not excluded (force-lit unless `worldPlaced_ &&
reVoxelize_`). canvas_stress floaters likely don't opt in. Verify gating-vs-
regression, then make detached *solids* participate (reusing the closed
#1375/#1576 paths). Parallel-safe with C1-C3 (different gating path) but land
C1-awareness so a newly-participating detached solid casts a clean (not ragged)
shadow.

### C1 — #2081 — Bake through the shared deformed-face footprint [CLOSED 2026-06-30 — premise falsified]
~~Re-architect the bake caster footprint to emit per-deformed-face footprints via
visibleTriplet + faceDeformationMatrix/deformedTrixelIsoPixel (residualYaw) +
yawedIsoDistance; sun basis is the only delta.~~ **Closed: the worker's verify-first
pass (PR #2109) showed the deform was never on the live caster path.** Under
residual yaw the GRID caster routes through the per-axis canvases
(`syncAllocationToCameraYaw` / `skipSingleCanvasVoxels`), keyed at the un-yawed
cardinal iso pixel with an inverse exact at every yaw — it recovers the TRUE
world position, not the un-yawed one; `faceDeform` is padding in the bake UBO.
The yaw-frozen mechanism (items 2/4) was already retired by #1719/#1435/#1311.
The only residual (dropped sub-cell frac offset + cardinal-vs-deformed silhouette
footprint) is edge/sub-pixel coverage → folded into C2. Items 2+4 marked
satisfied-by-prior-work.

### C2 — #2082 — Analytic edge-aware coverage in the bake [opus, chain head]
Extract scatterAnalyticEdgeCoverage into ir_iso_common; call from the bake;
0.5 coverage threshold (single-valued R32I depth). **Absorbs C1's residual:**
also thread the per-axis sub-cell frac offset through the resolve→bake footprint
so the cast silhouette tracks the deformed face. Closes item 3 + #1784.

### C3 — #2083 — Unify sun-space projection + cascade clipping [opus, depends C2]
Derive caster depth + receiver lookup from the same distance abstraction as the
render path (extend #1923's pos3DtoDistance unify to sun-space; parameterize the
axis); replace silent OOB cascade drop with clamp/grow-covering-cascade and make
split selection consistent for both faces of a straddling voxel. Closes item 5;
ties into #1923 / #1881 Child 3.

## Prerequisite / in-flight reconciliation

- **#2010 (revoxelize venetian gaps) is a geometry-coverage PREREQUISITE.** A
  wrong exposed-face mask feeds BOTH color and shadow; the C-series assumes a
  hole-free caster. Confirm + fix per `~/.fleet/plans/issue-2010.md` first.
- **#1718 / PR #1742 (sun-shadow step banding, receiver-side bias) is IN
  FLIGHT** (mac-opus-worker-1). It is the receiver-side facet; the C-series is
  caster-side and complementary. Do NOT disrupt #1742; re-baseline C-series
  acceptance shots after it lands.
- **#1923 (human:owned)** de-inlines pos3DtoDistance; C3 extends that unify to
  sun-space — coordinate so both don't rewrite the same distance helpers.
- **#1881 Child 3** (depth/clipping unification across render types) overlaps
  C3's cascade work — share findings, don't double-fix.
- **#1973** (curved-SDF self-shading, needs-human) is lighting-stylization,
  parked; out of this consolidation's scope.

## Backend note (all children)

Every shader fix lands on BOTH GLSL (engine/render/src/shaders) and Metal
(.../metal) and is smoke-validated on both (fleet:needs-macos-smoke /
fleet:needs-linux-smoke / fleet:needs-windows-smoke). Cardinal-0 fast paths stay
byte-identical.

## Dependency chain

#2010 (geometry coverage, prereq)
  -> C2 #2082 -> C3 #2083            (C1 #2081 CLOSED 2026-06-30 — residual folded into C2)
#2080 (detached) parallel-safe; coordinate with in-flight #1742 (#1718) and #1923.

## Acceptance (epic-level — from the #1717 body)

- A rotating re-voxelize / GRID cube under the canvas_stress sun shows faces
  shaded per-face (no row banding) across a full spin.
- Floor shadows track continuous camera yaw smoothly (no 90 deg snap) and deform
  consistently with the floor's residual squash/stretch.
- IRShapeDebug cube shadows: contiguous interiors (no streak holes) and
  silhouettes that read as the iso projection of the caster at every zoom shot.
- No full-face false shadows; no shadow leak through solid entities.


---

## Re-scope after verify-first design blocks (2026-06-27, steward)

The three verify-first spikes overturned the initial child framing:

- **#1718 (PR #2089)** = **AO facet** of the rotated-solid venetian banding
  (tilt-aware same-face resample in `c_compute_voxel_ao.glsl`; crease-preserving).
  NOT sun-shadow bias.
- **#2010 (PR #2088)** = sun-shadow **riser** facet of the same banding
  (step-aware receiver tolerance). NOT a geometry/mask defect; geometry is
  hole-free. Blocked by #1718 (re-measure residual after the AO fix).
- **#2080 (PR #2090)** = detached **receive** facet (tractable; not a gating gap
  — already world-placed by #1624). The detached **cast** facet is split to
  **#2091** (blocked on #1640 Metal R32I gap); the zero-caster floor self-shadow
  acne is split to **#2092** (sun-shadow-bake depth/bias; #1784 family).

Consequence for the C-series: **#2081/#2082 do NOT fix the venetian banding**
(that is AO + riser = #1718 / #2010). C1/C2/C3 remain scoped to the *other*
sun-shadow symptoms (yaw-frozen shadows, ragged silhouettes, swiss-cheese floor
/ acne).

Confirmed root model (grounded in the AO + sun-shadow shaders): a round-to-cell
re-voxelized solid becomes a true voxel staircase; AO darkens the tread→riser
*different-face* creases (`c_compute_voxel_ao.glsl:218` gate counts them), and
the sun-shadow term self-occludes risers (bias collapses to the slope floor on a
riser ⟂ sun). Both are real on different faces — which reconciles the two spikes.

**Open flag for the human:** #1640 is labelled `fleet:scope-shipped` yet the
Metal detached-cast symptom it covers persists — its true status needs a human
call before #2091 (cast) can proceed.


---

## Design decision (2026-06-27, human + steward) — GRID-only + canvas-governance ceiling

After the canvas-data-flow + representation audit:

**1. GRID vs detached split.** The venetian banding is on the re-voxelize staircase. Two paths:
- **GRID main-canvas** re-voxelize is PERMANENT (main-canvas SO(3) dead; (1,1,1) iso-depth invariant). Lighting fix is load-bearing → #1718 (AO) + #2010 (sun-shadow riser), **scoped GRID-only**.
- **DETACHED** staircase is REPLACED by #1444 (smooth deform, no staircase). Detached banding is routed to #1444 (P3 removes the staircase, P4 wires AO/shadow) — NOT patched. Decision: GRID-only fix, route detached to #1444.

**2. Canvas-governance ceiling.** Goal was "canvas data governs shadowing." Reality: the canvas stores depth + 2-bit slot + color, NOT deformed face geometry (per-pixel corners too expensive). Face-darkening + AO already derive from canvas (slot→faceId→normal). Sun-shadow bake/receive re-derive position via iso inversion that IGNORES faceDeform → they shadow the undeformed voxel (= the yaw-frozen-shadow bug). The achievable consolidation is **one shared deform/inverse-projection helper called by both the color raster and the shadow stages** (single source of truth for the LOGIC, kept in lockstep) — not a single stored buffer. C1 (#2081) re-scoped to require exactly this.

> **CORRECTION (2026-06-30, PR #2109 verify-first):** the premise above is wrong
> for the live path. The residual-yaw GRID caster does NOT go through the
> single-canvas `faceDeform` inversion — it routes per-axis and recovers the TRUE
> world position from an un-yawed cardinal iso key (exact at every yaw). The bake
> does not "shadow the undeformed voxel"; it only quantizes away the sub-cell frac
> offset. C1's deform-consolidation has no live path to act on and is CLOSED; the
> sub-pixel residual is folded into C2 (#2082). See the C-series section above.

**3. C-series de-conflation.** Only **C1 (#2081)** is the deform consolidation (shadow follows the deformed faces). **C2 (#2082, analytic coverage)** and **C3 (#2083, distance basis / cascade clipping)** are SEPARATE sun-shadow fixes (swiss-cheese, yaw-frozen-distance, missing-faces-at-edges) — not the deform consolidation; do not bundle.


---

## Cleanup debt — retire the per-pass staircase detectors (2026-06-29, architect + human) — WITHDRAWN 2026-06-30

> **WITHDRAWN.** This addendum assumed C1's analytic deform recovery could
> subsume the two detectors. The verify-first pass (PR #2109) showed both
> detectors are gated to the **static cardinal pose** — `detectSelfStepStaircase`
> at `!perAxis && residualYaw == 0.0` (`c_compute_sun_shadow.glsl:187`), AO
> beyond-resample at `!perAxis` (`c_compute_voxel_ao.glsl:240`) — where the deform
> matrix is **identity**. A deform reroute is a no-op there, so it cannot subsume
> them, and deleting them would regress the #2088/#2089 GRID venetian banding.
> **Both detectors stay.** The only surviving item is the optional interim:
> de-duplicate the two heuristics into one shared `isRoundToCellStaircase` in
> `ir_iso_common` — a pure refactor, separately ticketed if desired, NOT a
> deletion. The original text is retained below for history.

The GRID venetian-banding fixes shipped as **two independent screen-space
heuristics for the SAME round-to-cell staircase phenomenon**, each with its own
tuned constants — special-case logic the consolidation should subsume, not keep:

- **#1718 / PR #2089 (AO, merged)** — `c_compute_voxel_ao.glsl` "beyond-resample":
  when a same-face neighbour is ~1 voxel in front, sample one cell beyond; if the
  surface returns to its own face → staircase riser → skip the occluder.
- **#2010 / PR #2088 (sun-shadow)** — `c_compute_sun_shadow.{glsl,metal}`
  `detectSelfStepStaircase`: a *different* heuristic — probe 8 in-plane same-face
  neighbours; if any sits ~1 cell along the outward normal
  (`kSelfStepMinHeight/MaxHeight`) → staircase → recompute the cascade with the
  near rejection lifted (`kSelfStepDepthRange = 3.0`, threaded through
  `worldSunShadowFactor`/`sampleCascadeShadow`). Costs a second cascade eval +
  8 neighbour reconstructions on each shadowed staircase pixel.

Both are correct + byte-identity-safe + GRID-scoped, and were merged as targeted
fixes for a visible defect. But they are exactly the "custom logic for a specific
case" the consolidation exists to dissolve: **C1 (#2081)** routes the sun-shadow
recovery through the shared analytic deform/face primitives, so a round-to-cell
riser reconstructs at its true (deformed) position and **stops spuriously
self-occluding at the source** — making both detectors unnecessary.

**Required of the consolidation (C1, extending through the receive side):**
1. When the analytic recovery lands, **delete `detectSelfStepStaircase`** (+ its
   `kSelfStep*` constants and the `selfStepDepthRange` plumbing through
   `worldSunShadowFactor` / `sampleCascadeShadow`, both backends) **and #2089's
   AO beyond-resample** — the GRID spin cubes must stay band-free with neither
   detector present (that proves the analytic recovery subsumed them).
2. Cheaper interim step if the full analytic recovery slips: **unify the two into
   ONE shared `isRoundToCellStaircase` discriminator** in `ir_iso_common`, called
   by both the AO and sun-shadow passes, instead of two divergent heuristics.

**Acceptance addition:** a deliberate change to the shared analytic recovery
moves the GRID-cube shading with NO per-pass staircase detector compiled in;
removing both detectors keeps `--debug-overlay shadow` / AO clean across a full
spin; both backends; cardinal byte-identical.


---

## Steward ledger

reconciled-through: 2026-07-13 (heal-on-first-claim)
proposal-pending: none

### Children

Membership = union of all issues carrying `**Part of epic:** #1717`. The umbrella
had no `## Children` checklist since filing (unmanaged); healed 2026-07-13 —
closed children ticked in the umbrella body. This plan file was the architect's
local-staging copy, never committed to the repo; the heal commit syncs it in.

| Child | State | PR (closing) | Plan | Last validated |
|---|---|---|---|---|
| #1718 | merged (COMPLETED) | #2089 (AO facet; #1742 superseded) | epic plan | 2026-07-13 |
| #1719 | merged (COMPLETED) | #1723 | epic plan | 2026-07-13 |
| #1724 | merged (COMPLETED) | #1734 | epic plan | 2026-07-13 |
| #2010 | merged (COMPLETED) | #2088 (per plan; not auto-linked) | issue-2010.md | 2026-07-13 |
| #2080 | merged (COMPLETED) | #2090 | epic plan | 2026-07-13 |
| #2081 | closed-other (NOT_PLANNED) | #2109 (adjudication; not merged) | epic plan §C1 | 2026-07-13 |
| #2082 | merged (COMPLETED) | #2140 | epic plan §C2 | 2026-07-13 |
| #2083 | merged (COMPLETED) | #2275 | epic plan §C3 | 2026-07-13 |
| #2091 | open (blocked) | — | issue-2091.md | 2026-07-13 |
| #2092 | merged (COMPLETED) | #2308 (#2095 superseded) | epic plan | 2026-07-13 |

### Decisions
- D1 (2026-06-27): GRID-only staircase-lighting fix; the DETACHED staircase is routed to #1444 (smooth deform), NOT patched. — source: plan §"Design decision (2026-06-27) — GRID-only + canvas-governance ceiling".
- D2 (2026-06-30): C1 (#2081) deform-consolidation premise FALSIFIED — under residual yaw the GRID caster routes per-axis (`syncAllocationToCameraYaw`/`skipSingleCanvasVoxels`), recovers the true world position from an un-yawed cardinal iso key, so `faceDeform` was never on the live caster path. C1 closed NOT_PLANNED; the sub-pixel residual folded into C2 (#2082). — source: plan §C1 correction + PR #2109 architect adjudication.
- D3 (2026-06-30): the two per-pass staircase detectors (`detectSelfStepStaircase` from #2088, AO beyond-resample from #2089) STAY — both gate to the static cardinal pose where the deform is identity, so a reroute cannot subsume them. The "retire the detectors" cleanup-debt addendum is WITHDRAWN. — source: plan §"Cleanup debt … WITHDRAWN 2026-06-30".

### Events
- 2026-07-13: heal-on-first-claim (first steward claim of #1717). Built the `## Children` checklist from the 10 `**Part of epic:** #1717` back-refs (9 closed, 1 open); verified each state live. Synced the architect's local-staging plan into the repo (was never committed). No child scope prose edited.
- 2026-07-13: close-out NOT reached. One open child remains: **#2091** (detached re-voxelize world-shadow CAST) — blocked on the #1640 Metal R32I bake-input gap and carrying `fleet:needs-gl-host` (GL-host-bound; a macOS/Metal pane cannot advance it). Epic is 9/10 closed; the C-series (C2 #2082 / C3 #2083) landed, C1 #2081 folded into C2. Follow-on render-quality tracks live in open epics #1933 / #2331 (forked, not reopened here).
