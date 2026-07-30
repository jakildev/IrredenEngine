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
