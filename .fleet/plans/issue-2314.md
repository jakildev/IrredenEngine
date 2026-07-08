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

reconciled-through: 2026-07-08
proposal-pending: none

### Children
| Child | State | PR | Plan | Last validated |
|---|---|---|---|---|
| #2310 | open | #2313 | plan | 2026-07-08 |
| #2315 | open | — | plan | 2026-07-08 |
| #2316 | open | — | plan | 2026-07-08 |
| #2317 | open | — | plan | 2026-07-08 |
| #2318 | open | — | plan | 2026-07-08 |
| #2319 | open | — | plan | 2026-07-08 |
| #2320 | open | — | plan | 2026-07-08 |
| #2321 | open | — | plan | 2026-07-08 |
| #2322 | open | — | plan | 2026-07-08 |
| #2323 | open | — | plan | 2026-07-08 |

### Decisions
<!-- entries: D<n> (<YYYY-MM-DD>): <decision> — source: <link> -->
- D1 (2026-07-08): fix the real sun shadow instead of adding a contact/blob
  shadow system — source: 2026-07-08 design session (human).
- D2 (2026-07-08): spot cone shaping must be O(1) per pixel, no per-pixel
  ray marching; winning-light ID channel is the picked mechanism — source:
  2026-07-08 design session (human).
- D3 (2026-07-08): every child verifies at cardinal + ~30° + 45° yaw on
  both backends — source: 2026-07-08 design session (human).

### Events
- 2026-07-08: filed via file-epic (umbrella #2314, children #2315–#2323;
  #2310/PR #2313 pre-filed the same session as the first child).
