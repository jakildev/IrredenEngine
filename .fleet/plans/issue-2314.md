# Epic plan: lighting/shadow domain culling correctness + validation infrastructure (#2314)

- **Author:** opus-architect, 2026-07-08 design session (human in the loop
  throughout; human approved the phase structure, the hover-shadow
  direction, the no-marching spot constraint, and the yaw-matrix
  requirement in-session).
- **Relationship:** complements epic #1717 (shadow/lighting visual-quality
  symptom inventory). This epic owns (a) validation infrastructure that
  makes light/shadow domain culling verifiable by humans and agents, and
  (b) the correctness/quality series grounded empirically on 2026-07-08.
  #2310 (light-volume continuity: camera-anchor sign fix + boundary
  seeding + stable falloff + LINEAR sampling; PR #2313) is the epic's
  first child, filed and implemented the same session.

## Cross-cutting acceptance requirement (every child)

Verification at cardinal yaw AND non-cardinal yaw (~30° mid-bracket, 45°
bracket edge), both backends. Cardinal and residual yaw take different
code paths (single-canvas vs per-axis); a cardinal-only matrix cannot see
half the pipeline.

## Session ground truth (evidence: #2310 / PR #2313 + session captures)

- Light volume: camera-anchor sign bug (anchored at the mirror of the
  viewed position), hard ±72 cull + 1/8-step fade band, gathered-set
  falloff instability, NEAREST shells — all fixed in #2310.
- Spot lights are point lights (direction/cone uploaded, never read).
- Sun-shadow receive: 2×2 binary PCF; 1024² map over the full 64-voxel
  sweep; per-face slope-bias wedge seams; kMaxShadowDepthRange = 24 vs
  feeder sweep 64 (floating top-face truncation).
- Under-entity artifacts attributed via the shape_debug overlay
  discriminator: same-face banding = grazing self-hits; symmetric wedges =
  per-face bias seams; AO = thin contact chevrons only. Human decision:
  fix the real shadow, no contact/blob shadow system.
- Detached: receive is opt-in per spawn site (silent raw-albedo on a
  miss); free-SO(3) shadow projection untracked.

## Children

| Phase | Issue | Title | Model | Blocked by |
|---|---|---|---|---|
| L1 | #2310 | light-volume continuity (PR #2313, in flight) | opus | (none) |
| V1 | #2315 | domain instrumentation + freeze extension | sonnet | (none) |
| V2 | #2316 | minimap caster + light domains | sonnet | #2315 |
| V3 | #2317 | light-verify harness + hover sweep | sonnet | #2316 |
| L2 | #2318 | spot cone via winning-light ID channel | opus | #2310 |
| S1 | #2319 | unify per-face bias + kill grazing self-hits | opus | #2270, #2207 |
| S2 | #2320 | unify the shadow-throw limit | opus | #2319 |
| S3 | #2321 | PCF width + zoom-aware texel budget | opus | #2320 |
| D1 | #2322 | detached world-lighting by default | sonnet | (none) |
| D3 | #2323 | SO(3) detached shadow projection (spike) | opus | (none) |

Dependency chains: V1→V2→V3; #2310→L2; (#2270,#2207)→S1→S2→S3; D1, D3
independent heads.

## Out of scope (recorded decisions)

- Contact/blob shadow system (human: fix the real shadow — S-series).
- Light-volume zoom-coverage limitation (±64 window vs far-zoom viewport)
  — accepted; revisit with V3 data.
- Per-light falloff curves — unlocked by L2's ID channel; file after L2.

## Steward ledger

reconciled-through: **PR #2654 merge (2026-08-04T04:15:02Z, master `34c7f7f4`)** — #2385 Phase-0 (r7) shipped. Note the shape: the PR merged and **the child stayed open** (`[WIP]` in its own title; no `Closes #2385`), so no `rollup` trigger ever fired — the projection reported this epic as `[9/11]` with zero pending work for four days. Reconciled here by reading the ledger's own `#2385` row (which still said "PR #2654 open, wip, CONFLICTING") against the PR's live state. Prior: flow-a triage of PR #2654 / #2385 Phase-0 (2026-07-30) — all questions derivable, `design-unblock` applied, child plan amended A1, no proposal package; PR #2387 merge (2026-07-14 — S2 #2320 shadow-throw unify merged; V3 #2317 + S1 #2319 reconciled prior iterations).
proposal-pending: **OPEN** — `## STEWARD PROPOSAL 2026-08-08` on this umbrella (https://github.com/jakildev/IrredenEngine/issues/2314#issuecomment-5227813732), one question (does r7 discharge D6, or does #2385 close as a partial with a successor child?), `fleet:steward-proposal` applied; its removal is the re-fire edge. This **re-raises, with a label, the question the 2026-07-30 escalation asked without one**: that comment (issuecomment-5134608422) was explicitly filed `non-blocking` and added no label, and it went unanswered for nine days while r7 shipped underneath it — the exact leak documented in `~/.fleet/feedback/epic-steward.md` (2026-08-08, "comment-only escalation paths have no Decisions-surface step"). The question is now blocking rather than advisory: r7 is on master, so "ship r7 and let D8's re-measure decide the rest" is no longer a recommendation about a future merge but a live choice about what closes #2385. Prior: STEWARD PROPOSAL 2026-07-14 (PR #2393 / S3 #2321) answered by opus-architect 2026-07-15 (issuecomment-4977022751) and distributed (D7–D9; child plan `issue-2321.md` A1; `## Steward direction` on PR #2393; `fleet-transition design-unblock 2393`; #2321 re-blocked on #2385; #2385 routed to the planning gate). Prior 2026-07-13 package (PR #2343 / S1 #2319) answered + distributed (D4–D6, `## Steward direction` on PR #2343).

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2310 | merged | #2313 | plan | 2026-07-13 |
| #2315 | merged | #2347 | plan | 2026-07-13 |
| #2316 | merged | #2353 | plan | 2026-07-13 (this rollup — V2 culling-minimap caster/light domains) |
| #2317 | merged | #2356 | plan | 2026-07-14 (V3 light-verify harness; domain matrix covers yaw0/30/45 → satisfies D3) |
| #2318 | merged | #2337 | plan | 2026-07-13 |
| #2319 | merged | #2343 | plan | 2026-07-14 (S1 same-plane provenance test; D6 genuine-cast residual → #2385; Linux/GL smoke owed) |
| #2320 | merged | #2387 | plan | 2026-07-14 (S2 — receive window unified 24→64 feeder sweep; floating top-face wedge restored) |
| #2321 | open — `fleet:blocked` on #2385 (design answered, deferred) | #2393 | plan (issue-2321.md, A1 + **A2**) | 2026-08-08 (re-validated against PR #2654: D9's "first post-#2385 baseline capture" anchor pinned to master ≥ `34c7f7f4` — A2; `fleet:blocked` stands per D7, #2385 is still open) |
| #2322 | merged | #2328 | plan | 2026-07-13 |
| #2323 | merged | #2326 | plan | 2026-07-13 |
| #2385 | open — **Phase-0 shipped, child NOT closed** (partial PR, no `Closes`); queued, `fleet:needs-gl-host`; still the critical path | #2654 **merged 2026-08-04** (master `34c7f7f4`) | **plan** (v2 `## Plan` comment; `issue-2385.md` = pointer + A1 + **A2**) | 2026-08-08 (post-merge audit: AC 4 visual half **discharged**; AC 7 two-host re-bless **not** discharged — only `macos-debug` re-blessed, see F1; residual = the linux re-bless + the pending Phase-1 ruling) |

### Decisions
<!-- entries: D<n> (<YYYY-MM-DD>): <decision> — source: <link> -->
- D1 (2026-07-08): fix the real sun shadow instead of adding a contact/blob
  shadow system — source: 2026-07-08 design session (human).
- D2 (2026-07-08): spot cone shaping must be O(1) per pixel, no per-pixel
  ray marching; winning-light ID channel is the picked mechanism — source:
  2026-07-08 design session (human).
- D3 (2026-07-08): every child verifies at cardinal + ~30° + 45° yaw on
  both backends — source: 2026-07-08 design session (human).
- D4 (2026-07-13, final form): sun-depth pack low 8 bits = splat displacement
  vector (`depth << 8 | dx:4 | dy:4`); receive-side rejection is
  same-plane-at-origin on splat taps only; widened thresholds of any scope
  (global, scalar) are measured-refuted — source: architect answer on the
  #2314 STEWARD PROPOSAL 2026-07-13 thread (PR #2343 rounds 1–3).
- D5 (2026-07-13): host-conditional floor self-acne removal is IN SCOPE for S1
  #2319 (it is the same defect as the cube-top self-hit — a splat-displaced
  coplanar write read back as an occluder); macOS S-series acceptance oracles
  are re-grounded — zero-caster flat floor = 0 shadow px (primary gate),
  `sunSplatMaxTexels=0` splat-off master = the genuine-cast lower bound; the
  full-scene `88380` macOS anchor is contaminated (~64k acne + ~24k cast) and
  retired — source: architect answer, #2314 thread 2026-07-13.
- D6 (2026-07-13): the genuine-cast under-coverage residual unmasked by S1's
  same-plane test is ACCEPTED for #2319 — it is #2270-lineage splat coverage
  (not receive correctness) and files as a new unlabeled child at post-merge
  reconcile (flow b), citing PR #2343's measurements; #2270/#2092 stay closed
  — source: architect answer, #2314 thread 2026-07-13.
- D7 (2026-07-15): sequence #2385 BEFORE S3 #2321 — #2321 gains
  `Blocked by: #2385`; #2385 is now the epic's critical path. S3's
  render-shadow-metric (components/largest_frac) gate is contaminated by the
  #2385 bake-coverage honeycomb (missing sun-map writes, D6-recorded
  #2270-lineage coverage) that S3's receive/extent levers cannot touch, so the
  gate is un-measurable on this baseline; land #2385 first for a clean oracle.
  Option (b) ROI-exclude the honeycomb = metric gerrymandering; (c) scope-fold
  hides #2385's own acceptance behind S3 — both rejected — source: architect
  answer, #2314 thread 2026-07-15 (issuecomment-4977022751).
- D8 (2026-07-15): the architecture direction for materially finer on-screen
  sun shadows is DEFERRED — re-measure on the post-#2385 baseline before
  committing. The FAR cascade (serving iso-depth ≈ 0 on-screen receivers while
  pinned to its full slab as the covering fallback) is the limiter, so finer
  on-screen shadows need an architecture change, not an extent tweak. Non-binding
  cheapest-first order if still wanted after #2385: (c) content-fit split retune
  (no buffer/UBO cost) → (a) 2048² sun map (≈4× memory; check Metal buffer
  budget) → (b) 3rd cascade (most invasive). If the post-#2385 zoom 1–2 metric
  already reads clean, close the "finer resolution" want with a measurement
  citation instead of building any — source: architect answer, #2314 thread
  2026-07-15.
- D9 (2026-07-15): S3 #2321 plan amended append-only (`issue-2321.md` A1) —
  lever (a) (zoom-aware near-cascade extent fit) DROPPED as measured-refuted
  (structural no-op; byte-identical A/B on Metal, components 88/71 &
  largest_frac 0.88/0.48 unchanged at z2/z4); lever (b) (3×3 PCF receive kernel)
  RETAINED (keep S1 splat/bias untouched per D4); the blind "≥2×" gate
  re-anchored to "material, measured improvement in components/largest_frac at
  zoom 1–2, numeric target from the FIRST post-#2385 baseline capture" (the ≥2×
  ratio was calibrated to a honeycomb-dominated metric) — source: architect
  answer, #2314 thread 2026-07-15.

### Events
- 2026-07-08: filed via file-epic (umbrella #2314, children #2315–#2323;
  #2310/PR #2313 pre-filed the same session as the first child).
- 2026-07-13: ledger resynced from the drifted 2026-07-08 snapshot (all rows
  still read "open") to current states — merged since filing: #2310 (#2313),
  #2315 (#2347, V1 instrumentation), #2316 (#2353, V2 minimap — this rollup
  trigger), #2318 (#2337, L2 spot cone), #2322 (#2328, D1 detached lighting),
  #2323 (#2326, D3 SO(3) spike).
- 2026-07-13: STEWARD PROPOSAL 2026-07-13 (PR #2343 / S1 #2319) answered by
  opus-architect and distributed — `## Steward direction` posted on PR #2343,
  `fleet-transition design-unblock 2343`, D4–D6 recorded. The architect
  removed `fleet:steward-proposal` at 21:52 but it was spuriously re-added at
  21:59 with no comment (steward-release over-broad-prefix artifact), which had
  stranded the answered proposal; cleared this iteration.
- 2026-07-13: OWED at PR #2343 merge — file the D6 follow-up child
  ("genuine-cast under-coverage unmasked by S1: re-fix in-map coverage on the
  decontaminated baseline"), unlabeled → planning gate, citing PR #2343's
  cast-ROI measurements. **Discharged 2026-07-14 → #2385 (see below).**
- 2026-07-14 (flow b — #2319 rollup): PR #2343 merged (mergeCommit b2248fd7,
  2026-07-14T04:43Z) → #2319 checkbox ticked + ledger row set to merged.
  Scope-drift audit: matches D4 (packSunDepth displacement vector + receiver
  same-plane test, twinned GLSL/Metal, 6 shader files); no contradiction of a
  recorded Decision. Linux/GL smoke (gate 5) still owed
  (`fleet:needs-linux-smoke` — GL twin unbuilt on the macOS pane).
- 2026-07-14 (flow b — D6 discharge): filed the genuine-cast under-coverage
  follow-up child as #2385 (unlabeled → planning gate; `**Part of epic:** #2314`,
  Model opus), citing PR #2343's cast-ROI numbers (S1 24400 px / 59 comp /
  0.7705 vs splat-off baseline 5056 / 93 / 0.3418). Pending flow-c adoption
  into the checklist next iteration.
- 2026-07-14 (flow b — sibling re-validation): #2320 (S2) auto-unblocked when
  #2319 closed (`fleet:blocked` cleared); its inline-body premise holds vs the
  landed same-plane mechanism — no amendment needed. #2321 (S3) stays blocked
  by #2320.
- 2026-07-14 (bookkeeping): this docs PR is cut fresh off origin/master and
  supersedes the prior-iteration ledger PR #2371 (which conflicted after the
  #2365 reconcile landed to master); #2371 closed as superseded.
- 2026-07-14 (flow b — #2317 rollup): PR #2356 merged (2026-07-14T16:52Z,
  stateReason COMPLETED) → #2317 checkbox ticked + ledger row set to merged.
  Scope-drift audit: V3 light-verify harness — `scripts/light-verify.py`,
  `lighting_demo_scene.hpp`, `auto_screenshot.hpp`, the light-verify reference
  images (domain-matrix z1/z2/z4/z8 × yaw0/30/45 × in-win/band/beyond, hover
  sweep, light-boundary sweep), plus `engine/render/CLAUDE.md` + render-debug-loop
  diagnosis doc. Pure validation infrastructure (V-series); in-scope and
  additive; contradicts no recorded Decision. The domain matrix's yaw0/30/45
  coverage is exactly the D3 cross-cutting acceptance requirement, so the
  harness now gives S2 #2320 / S3 #2321 a concrete both-yaw acceptance oracle —
  no sibling plan amendment needed (harness is additive; renames/removes no
  symbol either plan cites).
- 2026-07-14 (flow c — #2385 adoption): #2385 (the D6 discharge child filed
  2026-07-14) carries `**Part of epic:** #2314` + `**Model:** opus` and was
  absent from the checklist → adopted. `fleet-validate-stack 2314` PASS on all
  three open children (#2320/#2321/#2385, required structured fields present);
  no machine-line fixes needed. Appended `- [ ] #2385` to the umbrella
  `## Children`; committed a planning stub at `.fleet/plans/issue-2385.md`
  (unlabeled → planning gate, so a worker cannot claim it unplanned).
- 2026-07-14 (flow b — #2320 rollup): PR #2387 merged (mergeCommit fea3ffbb,
  2026-07-14; "Closes #2320") → #2320 checkbox ticked + ledger row set to merged.
  Scope-drift audit: matches the S2 plan exactly — the receive-side throw window
  is unified to the feeder sweep (PR screenshots `composite_before_window24` →
  `composite_after_window64` confirm 24→64), twinned GLSL/Metal across the
  sun-shadow bake / sample / AO shaders (`c_bake_sun_shadow_map`,
  `c_compute_sun_shadow`, `ir_sun_shadow_sample`, `c_compute_voxel_ao`) +
  `ir_render_types.hpp` + `engine/render/CLAUDE.md`. The AO-shader twin touch is
  the shared `ir_sun_shadow_sample` include propagation the #2320 plan's
  fog/AO gotcha anticipated; in-scope, additive, contradicts no recorded Decision
  (D1–D6). Sibling re-validation: #2321 (S3) auto-unblocked when #2320 closed
  (`fleet:blocked` cleared, `fleet:queued` present); its approach is explicitly
  "evaluated on post-S2 master" and its dependency note ("the throw distance sets
  the swept AABB the cascade extents must cover") is now concretely realized —
  premise satisfied, not stale (PR #2387 renamed/removed no symbol #2321 cites),
  no amendment. #2385 (S1-follow-up/D6) stays open, unlabeled planning stub.
- 2026-07-14 (flow a — S3 #2321 design-block triage): PR #2393 (worker opus,
  macOS) raised a `NEEDS-DESIGN` reporting that the plan's lever (a) (zoom-aware
  near-cascade extent fit) is measured-refuted on Metal — a structural no-op:
  the near slab clamps to today's full 204.8-voxel slab at every tested zoom
  (visible iso-depth span 341–1364 ≫ 204.8), and the on-screen receivers (the
  central cluster floor at iso-depth ≈ 0) are served by the FAR cascade, pinned
  to its full slab as the covering fallback — so the shadow output is
  byte-identical. Sun texels already track zoom sub-linearly (near 0.45→0.15
  across zoom 0.55→4), and the residual zoom-4 jaggedness is a hexagonal
  honeycomb of unshadowed floor holes = the #2385 under-coverage signature
  (missing sun-map writes, D6-recorded #2270-lineage splat coverage), which
  S3's ≥2× render-shadow-metric gate is defined on and its receive/extent
  levers do not touch. All 3 questions — (1) sequence #2385 before S3? (2)
  finer-resolution architecture direction (2048² / 3rd cascade / split retune)?
  (3) amend vs re-scope #2321? — are NOVEL: none citable to the plan or D1–D6
  (the plan sequences S3 only behind S2 #2320; #2385 has no declared ordering
  vs S3; the architecture direction is undecided). Parked:
  `fleet-transition design-propose 2393` (design-blocked→design-proposed),
  aggregated STEWARD PROPOSAL 2026-07-14 posted on #2314 (recommendations: seq
  #2385 first; defer the architecture choice until a post-#2385 re-measure;
  amend #2321 append-only dropping lever (a), retaining lever (b) 3×3 PCF),
  `fleet:steward-proposal` added to the umbrella. Re-fire edge = removal of
  `fleet:steward-proposal`; distribution then amends the #2321 plan record
  citing the answers, posts `## Steward direction` on #2393, and
  `fleet-transition design-unblock 2393`.
- 2026-07-15 (flow a — S3 #2321 distribute): STEWARD PROPOSAL 2026-07-14
  answered by opus-architect (issuecomment-4977022751, 2026-07-15T05:00:57Z;
  `fleet:steward-proposal` removed = re-fire edge). All 3 answers accepted the
  steward's recommendations and are recorded as D7–D9. Distributed: (1) created
  per-child plan `issue-2321.md` with amendment A1 (drop lever (a)
  measured-refuted, retain lever (b), re-anchor the gate on the post-#2385
  baseline); (2) edited #2321 body `**Blocked by:** #2320` → `**Blocked by:**
  #2385` and flipped its labels `fleet:queued` → `fleet:blocked` (stale-queued
  from the #2320 auto-unblock; re-blocked on #2385 so no worker restarts #2393
  prematurely — its claim was already released at the NEEDS-DESIGN escalation);
  (3) routed #2385 to the planning gate — applied `fleet:task` + `fleet:opus` +
  `fleet:needs-plan` (architect "queue #2385" directive; #2385 is a STUB, so it
  enters via the planning gate — an opus planner must expand it before any
  worker claims it — NOT direct `fleet:queued`; `human:*` left untouched for the
  human); (4) posted `## Steward direction` on PR #2393; (5) `fleet-transition
  design-unblock 2393` (design answered — `fleet:design-proposed` →
  `fleet:design-unblocked`; PR stays `fleet:wip`, parked by the #2385 blocker
  until #2385 lands, then a worker re-arms and resumes #2393 with lever (b) on
  the decontaminated baseline).
- 2026-07-30: **both remaining children were parked on a `fleet:scope-shipped`
  FALSE POSITIVE — repaired.** No trigger surfaced this: the projection fires
  `closeout` only when every child is *closed*, and #2321/#2385 were open but
  dequeued, so epic #2314 sat invisible to the steward surface for 10 days. Found
  by live-checking the labels of every unticked child rather than trusting the
  cache's trigger list.
  - **The evidence.** `fleet-queue-ingest`'s scope check stamped #2385 citing
    merged PR **#2392** ("docs/fleet: epic-steward — #2317 rollup + #2385
    adoption (#2314)") and #2321 citing merged PR **#2396** ("docs/fleet:
    epic-steward — S3 #2321 design-block → proposal (#2314)"). Both are
    **all-`.fleet/` steward bookkeeping PRs** — #2392's entire diff is
    `.fleet/plans/issue-2314.md` + `.fleet/plans/issue-2385.md` (this ledger and
    #2385's own stub), #2396's is `.fleet/plans/issue-2314.md` alone. Neither
    shipped a line of either child's scope; the titles merely name the issues.
    Independently confirmed by PR search: the only PR that has ever carried
    #2321's implementation is **#2393, still OPEN**, and **no PR has ever
    implemented #2385**.
  - **The damage, timed.** #2385's stamp fired **13 s** after the human cleared
    its approach (01:49:53Z → 01:50:06Z), and again **49 s** after the v2
    plan-review cleared (11:02:25Z → 11:03:14Z). The v2 plan comment itself names
    the first bounce ("post-approval ingest churn bounced the issue back to
    `fleet:needs-plan`"). The second stamp stuck. So the epic's declared critical
    path (D7) has been fully planned, human-approved, and unpickable since
    2026-07-20 — and #2321/PR #2393 sat blocked behind it.
  - **Root cause already fixed, sweep never ran.** PR **#2464** ("fleet:
    scope-shipped skips all-`.fleet/` steward bookkeeping PRs") merged 2026-07-20
    **20:26:38Z** — ~9 h after the stamp that stuck. The predicate no longer fires
    on this class; these two issues are simply un-swept residue of the old bug.
    Nothing here needs re-deciding, re-planning, or escalating.
  - **Repair applied.** `fleet:scope-shipped` removed from both.
    #2385 → `fleet:queued` (it is planned and approved; class `fleet:opus`
    already set). #2321 → `fleet:blocked` (its body's `Blocked by: #2385` and D7
    both stand; the label had been lost to the park). `human:*` untouched.
  - **Stale plan artifact corrected.** `.fleet/plans/issue-2385.md` still read
    `## Plan status: STUB — needs planning before claim` — the steward's own
    2026-07-14 flow-c stub, false since 2026-07-20 and precisely the file whose
    presence in #2392 caused the false match. Rewritten as a **POINTER** to the
    canonical v2 `## Plan` comment (issuecomment-5021470607) with the approval
    trail, so the opus worker who now picks up #2385 cannot read it as unplanned.
    The plan itself was neither copied nor altered — the comment stays canonical.
  - **No Decision recorded:** this is a tooling-artifact repair, not a design
    call. D6–D9 stand unchanged and uncontradicted.
- 2026-07-30 (flow a — #2385 design-block triage, PR #2654): the repair above
  worked — an opus worker picked #2385 within hours and measured Phase 0
  (macOS/Metal, cardinal freeze, cast-ROI `1010,540,450,250`): r0 6936 px /
  91 comp / 0.3230 · r6 31320 / 53 / 0.9203 · r7 32144 / 47 / 0.9276. Premise
  CONFIRMED (components fall −11.3 % while cast px rises only +2.6 % = fill, not
  halo); both controls run and green (A==A byte-identical; the
  `kSunSplatMaxTexels = 0` kill switch collapses the cast). **All questions
  DERIVABLE — no proposal package, no park.**
  - **AC 5 (the blocker).** The worker found the six `canvas_stress` default
    shots cited as the per-axis byte-identity probes all carry **world-placed
    detached solids**, whose cast resolve (P4b-3) deliberately keeps the splat
    engaged — so *any* radius change moves their edges and AC 5 as written is
    unsatisfiable. Answered by citation, not synthesis: master's
    `docs/design/sun-shadow-bake-coverage.md` § "Byte-identity regimes" already
    states "(The world-placed cast resolve is deliberately **not** part of this
    regime … it is a separate feature whose cast the splat is meant to cover)",
    and `system_bake_sun_shadow_map.hpp:63-68` scopes the structural guarantee to
    the PER-AXIS resolve. Worker's Option A adopted; Option B (confine the bump
    to the cardinal main-canvas bake) refuted as a regression against that
    recorded position.
  - **AC 1.** Confirmed stale — and never valid cross-session: the splat-off
    *reference* itself moved 5056 → 6936 px / 0.3418 → 0.3230 since 2026-07-14,
    which is exactly what the plan's same-session-A/B gotcha exists to cancel.
    Re-grounded on the criterion's own same-session anchor; r7 PASSES.
  - **Phase 1 skipped** (plan early exit fired; r7 is under the #2204 waived r8
    ceiling, so no fresh cost waiver is owed).
  - **Still owed before merge, both derivable:** AC 4's *visual* half (the plan's
    "the halo guard is not optional") captured on a shot that shows the delta —
    `shadow_overlay_floor` measured zero change, so `revoxelize_solids` /
    `so3_offsnap_wide`; and a **two-host** re-bless — `linux-debug/`'s six
    references stale alongside `macos-debug/`'s and cannot be regenerated on the
    macOS pane, so AC 7 becomes a blocking co-requisite, not a routine smoke.
  - Distributed: `## Steward direction` on PR #2654 (issuecomment-5134594811);
    `.fleet/plans/issue-2385.md` amendment **A1**; `fleet-transition
    design-unblock 2654` (verified: `fleet:design-blocked` →
    `fleet:design-unblocked`, PR stays `fleet:wip` — it is the worker's to
    finish).
  - **Escalated, non-blocking** (issuecomment-5134608422): master r6 clears
    AC 1's *original* bar with zero change, so the criterion could never have
    failed the state it was written against — and 47 components is still a
    fragmented cast against the issue's "toward 1.0" goal. Question for the
    architect: does r7 discharge D6, or does #2385 close as a partial with a
    successor child? Steward recommendation: ship r7 and let **D8**'s reserved
    post-#2385 re-measure rule on the remainder rather than pre-empting it.
    No `fleet:steward-proposal` added; no Decision recorded — D1–D9 stand.
  - **Null-surface sweep** (per the #2385 lesson): live-checked every unticked
    child of every epic carrying no trigger — engine #1938/#1939/#2091/#2547/
    #2548/#2321 and game #202/#295/#299–#305. All are legitimately queued,
    host-gated, blocked, or claimed-in-progress. No second parked false-free
    this iteration.
- 2026-08-08 (flow b — **#2385 Phase-0 merged, child stayed open**): PR #2654
  merged 2026-08-04T04:15:02Z (master `34c7f7f4`, base `master`, head
  `claude/2385-sun-splat-coverage`). **No checkbox ticked and no rollup trigger
  fired** — the PR is a partial (`[WIP]` in its own title, no `Closes #2385`), so
  #2385 is still open and the projection kept reporting `[9/11]` with zero pending
  work. The ledger's own `#2385` row ("PR #2654 open, wip, CONFLICTING") is what
  surfaced it. Reconciled here; the checklist is untouched because the child is
  correctly still open.
  - **Scope-drift audit — in scope, and the two deltas both go the right way.**
    Shipped: the r6→r7 radius bump in `system_bake_sun_shadow_map.hpp` and both
    shader twins (`ir_sun_projection.glsl` / `.metal`, +6/−3 each — twinned, so
    the GL side moves too), `docs/design/sun-shadow-bake-coverage.md`, the six
    `macos-debug` `canvas_stress` references, the plan file, and the AC-4
    screenshot set. Nothing outside #2385's plan; no recorded Decision
    contradicted (D1–D9 stand).
  - **AC 4 (halo guard, visual half) — DISCHARGED.** A1 made this
    non-optional and named the two shots that could show a delta; the merged PR
    carries `revoxelize_solids` and `so3_offsnap_wide` before/after/diff plus a
    `revoxelize_solids_floorcrop3x` pair, and reports the newly-shadowed rim as
    1–2 device px at full shadow luminance (85.5 vs deep shadow 77.3, lit floor
    111.5), flat across distance, with the pre-existing r6 outermost ring at 88.8
    — i.e. a crisp one-cell edge shift, not a soft band. Verdict fill, not halo,
    on the instrument A1 specified.
  - **AC 7 (two-host re-bless) — NOT discharged; this is the drift.** A1
    escalated it from "owed" to a **blocking co-requisite** ("`linux-debug/`'s six
    references stale alongside `macos-debug/`'s … not a routine post-merge
    smoke"). The PR re-blessed **only** `macos-debug`. Verified on master: the six
    `linux-debug/canvas_stress` references were last written by `95355bde2`
    (PR #1595, 2026-06-30) and no commit has touched that directory since PR
    #2654 merged, while the twinned GLSL is on master — so the GL reference set
    has been stale since 2026-08-04. Recorded as **F1** with its owner; not
    escalated as a contradiction, because nothing was lost: #2385 stayed open and
    a pre-existing queued issue already owns the re-bless.
  - **Sibling re-validation — #2321 (S3).** `fleet:blocked` on #2385 stands (D7).
    D9's gate is anchored on "the FIRST post-#2385 baseline capture", and that
    referent is now ambiguous: r7 is on master but #2385 is not closed. Amended
    `issue-2321.md` **A2** with the one unambiguous half — any baseline capture is
    valid only at master ≥ `34c7f7f4` — and left the "wait for more of #2385?"
    half to the proposal below rather than guessing. No symbol #2321 cites was
    renamed or removed by the merge, so nothing else in its plan is stale.
    Skip-guard: not engaged — #2393 is #2321's own PR and carries no
    `fleet:merger-cooldown` (checked live, not from the cache).
  - **Proposal raised** (this iteration's one package): `## STEWARD PROPOSAL
    2026-08-08`, re-asking the 2026-07-30 escalation's question **with**
    `fleet:steward-proposal` this time. See `proposal-pending`.

### Findings (close-out gate — beyond the checklist)

<!-- Steward-owned. Items close-out must resolve that no checklist row tracks.
     Same shape as the section in .fleet/plans/issue-2544.md. -->

- **F1 (new 2026-08-08) — the `linux-debug` `canvas_stress` references have been
  stale on master since r7 landed, and the debt survived only by luck.**
  PR #2654 shipped the radius bump in **both** shader twins (`ir_sun_projection.glsl`
  and `.metal`, +6/−3 each) and re-blessed **only** the six `macos-debug`
  references. The PR's own commit message states the change "is therefore not
  output-neutral for the six rotating shots", and `.fleet/plans/issue-2385.md`
  **A1** had escalated the two-host re-bless from "owed" to a **blocking
  co-requisite** before merge. Measured on master (`28012d3cc`): the six
  `creations/demos/canvas_stress/test/references/linux-debug/*.png` were last
  written by `95355bde2` (PR #1595, **2026-06-30**), and `git log
  34c7f7f4..origin/master -- <that dir>` is **empty**. So a clean-checkout
  `render-verify --target IRCanvasStress` on a Linux/OpenGL host fails its 6-shot
  pixel gate for a reason unrelated to whatever change is under test — a false-red
  trap for the next GL worker.
  **Owner — pre-existing, do not file a new ticket.** #1969 ("refresh stale
  canvas_stress linux-debug render-verify references", OPEN, `human:approved` +
  `fleet:queued` + `fleet:sonnet` + `fleet:needs-gl-host`) already owns the
  main-six refresh, and #2158 owns the two `compare_*` refs. #1969's rationale
  predates r7 (it was filed for ~30 PRs of cumulative drift plus the #1942→#1953
  revert), but blessing at current master discharges r7 as a side effect —
  **provided the bless is taken at master ≥ `34c7f7f4`.** Close-out cites #1969's
  completion at such a master, not #2385's closure.
  **Why "by luck":** the co-requisite's only live carriers are #2385 staying open
  (`fleet:needs-gl-host`) and #1969 existing. `fleet:needs-linux-smoke` on merged
  PR #2654 does **not** carry it — that lane builds and smoke-runs `IRShapeDebug`
  and never refreshes demo references (#1969's own body says so), so a
  `platform-catchup` pass would flip that label to `fleet:verified-linux` with the
  references still stale. If the proposal below closes #2385, this finding is what
  keeps the obligation attached to the epic.
- **F2 (new 2026-08-08) — F1's *class* is already filed; this is an engine
  instance of it.** #2530 ("deferred acceptance criteria vanish at merge — no
  needs-`<host>`-smoke gate when a PR defers verification to another host", OPEN,
  **unlabeled**) describes exactly this shape and records two downstream-repo
  instances. F1 is a third, in the engine, and a slightly worse one: the deferred
  criterion here was not merely written in a PR body, it was an explicit steward
  amendment (A1) that named it *blocking*, and it still merged undischarged.
  Recorded as a comment on #2530 rather than a new ticket. Not a close-out gate
  for this epic — F1 is; F2 exists so close-out records that the mechanism failure
  has an owner and is not re-derived from scratch next time.
- **F3 (standing) — no OpenGL host has verified the epic's shadow work.**
  §"Cross-cutting acceptance requirement (every child)" demands verification at
  cardinal **and** non-cardinal yaw on **both** backends, and **D3** repeats it as
  a ruling. Every measurement in this ledger from S1 onward — including PR #2654's
  r0/r6/r7 component counts — was taken on macOS/Metal. PR #2654 still carries
  `fleet:needs-linux-smoke` post-merge, and the epic's two remaining children
  (#2385, and #2321 behind it) both carry `fleet:needs-gl-host`. Tracked, not
  invisible; but close-out cannot discharge the both-backends clause from a macOS
  pane, and F1 means the GL host's own regression gate is currently red before it
  starts.
