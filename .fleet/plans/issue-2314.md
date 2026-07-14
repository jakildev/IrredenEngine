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

reconciled-through: PR #2343 merge (2026-07-14 — S1 #2319 merged; prior STEWARD PROPOSAL 2026-07-13 answered + distributed)
proposal-pending: none — the 2026-07-13 package (PR #2343 / S1 #2319) was answered by opus-architect and distributed 2026-07-13 (see D4–D6 and the `## Steward direction` comment on PR #2343)

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2310 | merged | #2313 | plan | 2026-07-13 |
| #2315 | merged | #2347 | plan | 2026-07-13 |
| #2316 | merged | #2353 | plan | 2026-07-13 (this rollup — V2 culling-minimap caster/light domains) |
| #2317 | open | — | plan | 2026-07-13 (V3; unblocked by #2316 merge) |
| #2318 | merged | #2337 | plan | 2026-07-13 |
| #2319 | merged | #2343 | plan | 2026-07-14 (S1 same-plane provenance test; D6 genuine-cast residual → #2385; Linux/GL smoke owed) |
| #2320 | open (unblocked — #2319 merged) | — | plan | 2026-07-14 (premise holds vs landed same-plane bias) |
| #2321 | open (blocked by #2320) | — | plan | 2026-07-14 |
| #2322 | merged | #2328 | plan | 2026-07-13 |
| #2323 | merged | #2326 | plan | 2026-07-13 |

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
