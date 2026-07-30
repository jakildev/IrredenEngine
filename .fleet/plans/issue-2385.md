# Plan — issue #2385

## Plan status: PLANNED — the canonical plan is the v2 `## Plan` comment on the issue

**Part of epic:** #2314 (render: lighting/shadow domain culling correctness + validation infrastructure)
**Model:** opus
**Blocked by:** (none) — #2385 is the epic's critical path; **#2321 (S3) is blocked on it** per D7

> **Read this first.** The plan of record for #2385 is the **v2 `## Plan` comment**
> on the issue:
> <https://github.com/jakildev/IrredenEngine/issues/2385#issuecomment-5021470607>
> (2026-07-20). It is human-approved and twice plan-review-cleared — see
> [approach sign-off, 2026-07-20T01:49Z](https://github.com/jakildev/IrredenEngine/issues/2385#issuecomment-5018096331)
> and [opus plan-review cleared on v2, 2026-07-20T11:02Z](https://github.com/jakildev/IrredenEngine/issues/2385#issuecomment-5021510774).
> This file is a pointer, not a second copy: the comment is canonical, and
> duplicating a 200-line plan here would only create a drift surface.

## Why this file changed (steward repair, 2026-07-30)

Until today this file read `## Plan status: STUB — needs planning before claim`
— the stub the epic-steward committed at flow-c adoption on 2026-07-14. That was
true then and **false since 2026-07-20**: #2385 was planned, reviewed, and
approved that day. The stub survived because the issue was simultaneously
knocked off the queue by a tooling false positive (below), so nobody came back
to it.

A worker claiming #2385 must not read this file as "unplanned". The v2 comment
is complete: verified current state, the committed one-approach phasing
(Phase 0 r7 in-nibble measure + premise gate with a documented bail path →
Phase 1 encoding widen + radius sweep), affected files, seven acceptance
criteria, sibling reconciliation, and gotchas.

## The false positive that dequeued this child for 10 days

`fleet-queue-ingest`'s scope check stamped `fleet:scope-shipped` on #2385 citing
merged PR **#2392** *"docs/fleet: epic-steward — #2317 rollup + #2385 adoption
(#2314)"*. #2392 is an **all-`.fleet/` steward bookkeeping PR** — its entire diff
is `.fleet/plans/issue-2314.md` and `.fleet/plans/issue-2385.md` (this file's
stub). It shipped none of #2385's scope; the title merely names the issue.

Timing shows the damage precisely:

| When (UTC) | Event |
|---|---|
| 2026-07-20 01:49:53 | human clears the approach (`human:review-plan`) |
| 2026-07-20 01:50:06 | **scope check false-stamps `fleet:scope-shipped`** — 13 s later |
| 2026-07-20 10:57:08 | planner re-posts as **v2** (A1 bit-layout fix from the human's caveat), noting the churn bounced the issue back to `fleet:needs-plan` |
| 2026-07-20 11:02:25 | opus plan-review clears v2 |
| 2026-07-20 11:03:14 | **scope check false-stamps again** — 49 s later; this one stuck |
| 2026-07-20 20:26:38 | PR **#2464** *"fleet: scope-shipped skips all-`.fleet/` steward bookkeeping PRs"* merges — the root-cause fix, ~9 h too late for this issue |

So the predicate bug is **already fixed**; #2385 and #2321 were simply never
swept afterwards. The steward removed both stale labels on 2026-07-30 and
restored `fleet:queued` on #2385 / `fleet:blocked` on #2321. No re-plan is owed
— the v2 plan stands exactly as approved.

## Provenance (unchanged, retained from the stub)

Filed as the **D6** discharge from epic #2314's S1 (#2319 / PR #2343) post-merge
reconcile: the same-plane provenance test S1 landed unmasked a genuine-cast
under-coverage residual that is **#2270-lineage splat coverage** (in-map cast
extent), **not** receive-side correctness. Per D6 it is accepted for #2319 and
re-fixed here on the decontaminated baseline. Sequencing is **D7**: #2385 lands
before S3 #2321, because S3's render-shadow-metric gate is contaminated by this
child's honeycomb and is un-measurable until it clears.

Measured basis the v2 plan builds on (from PR #2343, macOS/Metal):
- S1 same-plane cast ROI: 24400 px / 59 components / 0.7705 coverage.
- splat-off master lower bound: 5056 px / 93 components / 0.3418 coverage.

Verify at cardinal + ~30° + 45° yaw on both backends per **D3** (the V3
light-verify harness from #2317 — `scripts/light-verify.py` domain matrix — is
the acceptance oracle). Acceptance criterion 7 records the **Linux/GL smoke
owed**: the GL twin is unbuilt on the macOS pane.

## Amendments

### A1 — 2026-07-30 — trigger: flow-a design-block triage of PR #2654 (`## NEEDS-DESIGN`, Phase-0 measured)

- **Decision (AC 5 scope).** The **world-placed detached cast resolve (P4b-3) is
  OUT of scope for the radius-0 byte-identity invariant.** A radius change moves
  those solids' cast edges by design, so the six `canvas_stress` default shots —
  which all carry world-placed detached content — are not clean probes of the
  per-axis path AC 5 names. The invariant is a property of *paths* (the per-axis
  resolve, whose radius `patchSunSplatRadius` structurally zeroes), not of those
  shots. The worker's Option A is adopted; Option B (confining the bump to the
  cardinal main-canvas bake) is **refuted** — it would withhold the splat from
  the one other path the design doc says it exists to cover.
- **Decision (AC 1 grounding).** AC 1's absolute figures (59 comp / 0.7705 frac,
  PR #2343, 2026-07-14) are **stale and were never valid across sessions**: the
  splat-off *reference* itself moved 5056 → 6936 px / 0.3418 → 0.3230 frac in
  the interval. The criterion's operative clause is its same-session splat-off
  anchor. Evaluated that way, r7 (32144 px / 47 comp / 0.9276) clears it. AC 1
  **PASS**. Master r6 also clears the stale absolute bar with zero change — see
  the #2314 steward escalation (2026-07-30) for the epic-scope consequence;
  it does not gate this child.
- **Decision (Phase 1).** **Skipped** — the plan's early exit fired and r7 sits
  under the #2204 waived cost ceiling (r8, the architect anchor), so no fresh
  cost waiver is owed.
- **Decision (still owed before merge).** (a) AC 4's *visual* half is
  non-optional and must be captured on a shot that shows the delta —
  `shadow_overlay_floor` measured zero change, so use `revoxelize_solids` or
  `so3_offsnap_wide`. A soft dark border rejects r7 as halo regardless of the
  component count. (b) Re-blessing is a **two-host** job: `linux-debug/`'s six
  references stale alongside `macos-debug/`'s, and cannot be regenerated on the
  macOS pane — so AC 7 is a blocking co-requisite here, not a routine
  post-merge smoke.
- **Supersedes:** AC 5's shot-level wording ("per-axis `yaw30` / `yaw45`
  byte-identical") and `docs/design/sun-shadow-bake-coverage.md` § Acceptance
  oracle item 4's matching phrasing — both restated to cite the per-axis
  resolve's radius-0 property rather than the mixed-content shots. AC 1's
  absolute 59 / 0.7705 thresholds are superseded by the same-session anchor.
- **Acceptance criteria:** AC 1 re-grounded (same-session splat-off A/B; no
  absolute carry-over). AC 5 restated (scope = radius-0 paths). AC 4 unchanged
  but its shot selection is now specified. AC 7 escalated from "owed" to
  blocking co-requisite. AC 2 / AC 3 / AC 6 unchanged.
- **By:** epic-steward — sources: `docs/design/sun-shadow-bake-coverage.md`
  § "Byte-identity regimes" invariant 1 parenthetical ("The world-placed cast
  resolve is deliberately **not** part of this regime … it is a separate feature
  whose cast the splat is meant to cover");
  `engine/prefabs/irreden/render/systems/system_bake_sun_shadow_map.hpp:63-68`
  and `:263-265`; the v2 `## Plan` comment's gotchas ("the genuine-cast metric
  is a manual A/B — capture the splat-off baseline in the **same session** …";
  "the halo guard (criterion 4) is not optional") and its Phase-0 early exit;
  design doc § "The #2204 cost rule and the chosen radius" (r8 architect
  anchor); `creations/demos/canvas_stress/test/references/manifest.json` § notes
  (per-preset reference dirs). Distributed as `## Steward direction` on PR #2654
  (issuecomment-5134594811); `fleet-transition design-unblock 2654` applied.
