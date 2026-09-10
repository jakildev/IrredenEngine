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

reconciled-through: ruling distribution 2026-09-10 (architect ruling
2026-09-10T05:12:52Z on the 2026-09-09 package → **D6** here and **A2** on
`issue-2091.md`). Code-side unchanged: PR #3020 merge (2026-08-22T22:28:06Z) —
the Phase-0 partial on #2091 (`Refs`, not `Closes`, so no rollup ever fired);
#1640's 2026-07-09 closure folded into the same pass. **#2091 remains the sole
open child** and is now routed: Metal Phase 1, macOS-bound, queued.
proposal-pending: **none** — the [STEWARD PROPOSAL 2026-09-09](https://github.com/jakildev/IrredenEngine/issues/1717#issuecomment-5604759627)
was **answered 2026-09-10** by the architect ruling
https://github.com/jakildev/IrredenEngine/issues/1717#issuecomment-5613529971
(**option 1** — run Phase 1 on a macOS/Metal host; no partial close),
`fleet:steward-proposal` removed at 2026-09-10T05:12:54Z (the re-fire edge), and
**distributed the same day** as D6 here, A2 on `issue-2091.md`, and the
`## Steward direction` comment
https://github.com/jakildev/IrredenEngine/issues/2091#issuecomment-5614435566.
The package's no-rule fallback was therefore never reached. Its recommendation
(option 1, with the routing-gap class fix filed separately rather than sequenced
in front of it) was taken in full: the class fix is **#3146**.

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
| #2091 | open — **routed 2026-09-10** (ruling option 1: Metal Phase 1 on a macOS host, no partial close); Phase 0 DONE, Metal residual OPEN and macOS-bound | #3020 (Phase 0 only — `Refs`, not closing) | `## Plan` comment + `issue-2091.md` + A2 (was A1) | 2026-09-10 — ruling distribution |
| #2092 | merged (COMPLETED) | #2308 (#2095 superseded) | epic plan | 2026-07-13 |

### Decisions
- D1 (2026-06-27): GRID-only staircase-lighting fix; the DETACHED staircase is routed to #1444 (smooth deform), NOT patched. — source: plan §"Design decision (2026-06-27) — GRID-only + canvas-governance ceiling".
- D2 (2026-06-30): C1 (#2081) deform-consolidation premise FALSIFIED — under residual yaw the GRID caster routes per-axis (`syncAllocationToCameraYaw`/`skipSingleCanvasVoxels`), recovers the true world position from an un-yawed cardinal iso key, so `faceDeform` was never on the live caster path. C1 closed NOT_PLANNED; the sub-pixel residual folded into C2 (#2082). — source: plan §C1 correction + PR #2109 architect adjudication.
- D4 (2026-09-09): **#2091 is a Metal-only backend gap and is no longer #1640-gated; Phase 1's prescribed approach has no referent.** The plan's fork is settled at (a) by the Phase-0 measurement in PR #3020 (GL floor-ROI `shadow_px` 1189 single-component vs 0 floor-alone and 0 `--screen-lock-detached`; GRID control 4574). The `**Blocked by:** #1640` gate is discharged — #1640 CLOSED/COMPLETED 2026-07-09T02:29:51Z, closed by merged PR #2307 ("Metal headless GPU vehicle-A harness + R32I cross-encoder regression guard"), a harness plus a regression guard and **not** the "landed foreign-canvas R32I read mechanism" Phase 1 says to consume verbatim. Derivable, not a new position. — source: #2091 `## Plan` comment §Approach + §"Verified current state"; #2091 comment 2026-08-21T23:32:03Z; #1640 state + PR #2307 title. Recorded as #2091 amendment A1.
- D5 (2026-09-09): **the 2026-07-08 architect note's designated Phase-1 starting point (FrameData staleness) is REFUTED; the live candidate is the #2488 own-canvas `resolveImageAtomicScratch` gap.** `MetalBufferImpl::subData` (`engine/render/src/metal/metal_buffer.cpp:52-100`) orphans an already-encoded buffer, so a dispatch encoded against version *N* keeps reading *N* — the proposed mechanism cannot fire. The candidate that replaces it is narrower than the #1640 foreign-read gap this thread circled and post-dates every prior investigation on it. **It does not relax the resolve-then-bake invariant** (`engine/render/CLAUDE.md:1495-1497`): `c_resolve_world_placed_depth` IS the sanctioned resolve — Pass 1 of the block (`system_bake_sun_shadow_map.hpp:433-461`), so its foreign model-frame read is the *resolve's* input, not a bake input, and the bake still consumes the main-canvas-layout `worldPlacedResolveDepth_` (`:212`; Pass 2 blits it from the scratch at `:484`, Pass 3 binds it READ_ONLY as the bake's only depth input at `:503-504`). The candidate adds no foreign bake read and is not offered as a substitute for resolve-then-bake, which `engine/render/CLAUDE.md:1526-1530` forbids explicitly; it materializes the caster's OWN distances so the sanctioned resolve has real data to scatter. The resolve/bake split is easy to misread at this site; `issue-2091.md` A1 states it in full. Derivable, not a new position — the refutation is code-grounded and the candidate is the reporting pane's, recorded verbatim rather than restated. — source: #2091 comment 2026-08-22T23:07:34Z; `engine/render/CLAUDE.md` §#2488; `system_voxel_to_trixel.hpp:1949` (the single non-backend call site; re-resolved at this PR's base — the reporting comment cites the drifted `:1917`). Recorded as #2091 amendment A1.
- D3 (2026-06-30): the two per-pass staircase detectors (`detectSelfStepStaircase` from #2088, AO beyond-resample from #2089) STAY — both gate to the static cardinal pose where the deform is identity, so a reroute cannot subsume them. The "retire the detectors" cleanup-debt addendum is WITHDRAWN. — source: plan §"Cleanup debt … WITHDRAWN 2026-06-30".

- D6 (2026-09-10): **#2091's Metal Phase 1 runs on a macOS/Metal host; the epic does not close partial, and D4/D5 are dead ends that must not be re-chased.** Answering the 2026-09-09 package's single question (how the Metal residual gets routed), the architect took **option 1**. The designated first probe is the already-written line at `system_bake_sun_shadow_map.hpp:443` — verified on master, that is the per-caster loop head `for (const auto &caster : worldPlacedCasters_) {`, at `engine/prefabs/irreden/render/systems/system_bake_sun_shadow_map.hpp:443` (note the path is `engine/prefabs/irreden/render/`, not `engine/render/`). This **supersedes D5's** offer of the #2488 own-canvas `resolveImageAtomicScratch` gap as "the live candidate": the ruling closes off both D4 and D5 explicitly. The routing-gap class fix (the package's option 2) was directed to be filed free-standing rather than sequenced ahead of the work — filed as **#3146**, scoped per the ruling to the `fleet-claim` enforcement gap ("claim refuses a wrong-host pick for a `Host:`-pinned issue"), not a new label. — source: architect ruling https://github.com/jakildev/IrredenEngine/issues/1717#issuecomment-5613529971; probe line re-verified on master 2026-09-10. Recorded as #2091 amendment A2.
- D7 (2026-09-10): **the ruling was reversed by automation 64 seconds after it landed, and the reversal is a fleet-tooling defect, not a decision.** `fleet-queue-ingest` stamped `fleet:scope-shipped` on #2091 at 2026-09-10T05:13:58Z — 59 seconds after the same sweep removed `fleet:needs-human` to un-park it per option 1 — dequeuing the epic's sole open child and returning #1717 to the exact state (open, dequeued, un-parked, invisible to every trigger kind) that had already cost it 58 silent days. The stamp is **measured false**: the matched PR #3020's entire diff is one documentation file (`docs/design/detached-revoxelize-world-light.md`, +32/−2) and its body reads *"No `Closes #2091` — deliberate … Auto-closing on merge would strand it."* Executing the genuine-ship predicate against #3020's live title/body/files returns **True**, with layers 4, 6/7 and 8 all measured non-firing; swapping only the diff to `.fleet/plans/…` returns **False** (layer 8 fires) and the body alone returns **False** (layer 5 strips the backticked verb). Both controls armed, so the result is not vacuous: the only thing separating a correct rejection from the false stamp is **which documentation directory the single changed file sits in**. This is the undelivered half of **#2196**'s own selected remedy — *"exclude matched PRs whose titles mark deferral/documentation of the issue (e.g. contains `(#N deferred)` or a docs-only diff)"* — of which only the deferral half shipped as layer 6; and #2196 was filed **to protect #2091**. Filed as **#3145** (`fleet:agent-approved` + `fleet:plan-review`, plan posted at file time). Contained on this issue by hand: `fleet:scope-shipped` removed, `fleet:queued` restored — which sticks only because `fleet:queued` is itself a member of `fleet-state-scout`'s `_ALREADY_QUEUED_LABELS`, so #2091 no longer enters `pending_issues` for the pre-flight to re-examine. A bare removal would have re-stamped on the next pass, the "un-winnable label fight" #2196 named. — source: #2091 timeline 2026-09-10T05:12:59Z / 05:13:56Z / 05:13:58Z; PR #3020 `--json files` + body; `scripts/fleet/fleet_scope_shipped.py` layers 4/5/6/7/8 executed against live data; `scripts/fleet/fleet-state-scout` `_ALREADY_QUEUED_LABELS`; #2196 body §Ask option 2.

### Events
- 2026-07-13: heal-on-first-claim (first steward claim of #1717). Built the `## Children` checklist from the 10 `**Part of epic:** #1717` back-refs (9 closed, 1 open); verified each state live. Synced the architect's local-staging plan into the repo (was never committed). No child scope prose edited.
- 2026-07-13: close-out NOT reached. One open child remains: **#2091** (detached re-voxelize world-shadow CAST) — blocked on the #1640 Metal R32I bake-input gap and carrying `fleet:needs-gl-host` (GL-host-bound; a macOS/Metal pane cannot advance it). Epic is 9/10 closed; the C-series (C2 #2082 / C3 #2083) landed, C1 #2081 folded into C2. Follow-on render-quality tracks live in open epics #1933 / #2331 (forked, not reopened here).
- 2026-09-09: **Re-validation after 58 days — no trigger ever fired for this epic, and the ledger had gone from stale to inverted.** #2091 never closed (so no `rollup` was due) and 9/10 is not a `closeout`, so the projection showed nothing while the child moved twice. The 2026-07-13 Events entry below called #2091 "blocked on the #1640 Metal R32I bake-input gap and carrying `fleet:needs-gl-host` (GL-host-bound; a macOS/Metal pane cannot advance it)". Measured today: **#1640 closed 2026-07-09** — four days *before* that entry was written, so the blocker citation was already stale at write time; **`fleet:needs-gl-host` was removed 2026-08-21** by the GL pane that finished the gated slice; and the routing is **inverted** — Phase 0 settled the fork at (a), so the residual is Metal-only and *only* a macOS pane can advance it. A macOS pane reading this ledger would have skipped the one epic it is uniquely able to finish. `reconciled-through` advanced to the PR #3020 merge.
- 2026-09-09: **#2091 Plan-column claim was false — plan file materialized.** The row cited `issue-2091.md`; no such file existed on `master` (the child's plan is its `## Plan` comment). Per flow b's ledger-claims-are-backed rule (#2571), `.fleet/plans/issue-2091.md` was created as a **pointer plus amendment log** — not a second copy of the plan — and A1 appended to it. Recorded because the file also satisfies `fleet-queue-ingest::_plan_exists()`, a content-blind existence probe: that is correct here (#2091 is planned and signed off, and carries `fleet:needs-human`, not `fleet:needs-plan`), but a later reader should not mistake it for a planning stub. The file says so in its own §"Where the plan lives".
- 2026-09-09: **Acceptance re-audit on #2091 — 2 of 4 discharged, and the split is the close-out gate.** GL cast **MET** (Phase 0 / PR #3020, the discharge route the criterion itself names) and no-GRID-regression **MET** (same run's positive control). Metal cast **OPEN**. Both-backend render-verify reference shot **OPEN and unreachable while the Metal half is out** — verified against the tree, `creations/demos/canvas_stress/test/references/` carries `macos-debug/` (8 refs) and `linux-debug/` (6) and **no `windows-debug/`** set, so the GL host that measured Phase 0 cannot bless it either. Amendment **A1** delivered to both sites (the new plan file and a comment on #2091, the site a resuming worker reads).
- 2026-09-09: **Escalation — the routing gap outlived its owner (proposal Q1).** #2091 is out of autonomous pickup by correct decisions, not neglect: `fleet:needs-gl-host` was rightly removed once Phase 0 finished, and there is **no inverse label** — verified against `gh label list`, the vocabulary has `fleet:needs-gl-host` plus the PR-side smoke labels and **no issue-side macOS-host claim gate**. #2820 owned exactly this gap and carried the 2026-08-22 datapoint from the second pane it cost; it **closed COMPLETED 2026-09-06 via PR #3000**, which *narrowed* `fleet:needs-gl-host` with a backend-symmetric discriminator and did not add the inverse. So the class defect now has no owner while remaining this epic's only gate. Raised as the iteration's single proposal package (options: macOS pane / inverse gate / close partial; recommendation option 1). Not re-filed as an issue this iteration — #2820's closure is recent enough that a re-file without a ruling would read as a duplicate; the package asks whether to file it.
- 2026-09-09: **Scope of this pass.** Steward writes only: two comments, one label (`fleet:steward-proposal` on this umbrella), one new plan file, this ledger. No child body, child label, or PR branch was touched; `fleet:needs-human` on #2091 is the human's and stays.
- 2026-09-10: **Package answered and distributed the same day; the epic is routed, not parked.** Ruling 05:12:52Z (option 1 — macOS/Metal host, no partial close); `fleet:steward-proposal` removed 05:12:54Z. Distributed as **D6** + **D7** here, **A2** on `issue-2091.md`, and a `## Steward direction` comment on #2091. The ruling's own instruction to file the routing-gap class fix "next iteration" was executed: **#3146** (`fleet-claim` host enforcement), filed **unlabeled** for human triage rather than into the agent-approved lane, because the fleet-infra bar there is a fired incident and #3146 rests on a source read. Scope-drift audit: none — no child scope changed. **Still 9/10; the epic's close-out gate is unchanged and is now purely the Metal Phase 1.**
- 2026-09-10: **The find of this pass was invisible to every projection, including the one for this role.** #1717's epic-steward projection entry listed **zero** pending triggers throughout: #2091 is open (so no `rollup`), the epic is 9/10 (so no `closeout`), the ruling is issue-scoped with no `fleet:design-proposed` PR (so no flow-a re-fire), and `adoptable` was `[]`. The two edges that mattered — a `fleet:steward-proposal` **removal** and a `fleet:scope-shipped` **addition**, 62 seconds apart — are both label transitions on issues no trigger kind watches. Method that found them, worth reusing: read the ledger's own `proposal-pending` against live GitHub, and sweep every epic's open children against the cache's task buckets for rows that are **absent from all of them**. That sweep flagged 5 absent children across both repos; four were explained by in-flight packages or intended holds, and the fifth was this.
