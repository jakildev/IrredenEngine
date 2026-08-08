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

## Approach recap (from v2 — the comment stays canonical)

Increase the #2270 coverage-splat radius `kSunSplatMaxTexels`, widening the
`packSunDepth` displacement encoding only if the radius must exceed the nibble
cap. Phased so the free in-nibble measurement precedes any encoding change:
**Phase 0** bumps 6 → 7 and re-captures the genuine-cast metric, with an
early exit ("ship the constant bump alone, skip Phase 1") if r7 clears the bar
without ballooning cast-ROI px, and a documented bail to
`fleet:design-blocked` if r6 → r7 does not reduce component count at all;
**Phase 1** (only on Phase-0 failure) widens the displacement field 8 → 10
bits and sweeps r ∈ {7,9,11,13,15} for the smallest clearing radius.

Halo guard (both phases): the component / largest_frac metric *improves* with
over-extension, so a radius is rejected if cast-ROI px balloons relative to the
coverage gain, **and** `attach-screenshots` must show the cast edge staying
crisp. The visual half is not optional.

## Phase-0 measured outcome (2026-07-30, macOS/Metal, pool-2)

Harness: `IRCanvasStress --debug-overlay shadow --no-auto-rotate --no-spin
--auto-screenshot`, shot `shadow_overlay_floor` (g_allShots index 8),
`scripts/render-shadow-metric.py --roi 1010,540,450,250`.

**Controls both pass** (neither was run by the v1/v2 planning passes):

- **A==A determinism.** Two back-to-back runs of the same binary → the shot is
  **byte-identical**. The metric is trustworthy on this host for this shot
  under freeze flags.
- **Positive control.** `kSunSplatMaxTexels = 0` (documented kill switch)
  collapses the cast to 6936 px / 91 comp / 0.3230 frac — the splat path is
  live and the radius lever reaches this observable.

| config | shadow_px | components | largest_frac |
|---|---|---|---|
| r0 (splat off) | 6936 | 91 | 0.3230 |
| **r6 (master, today)** | **31320** | **53** | **0.9203** |
| **r7** | **32144** | **47** | **0.9276** |

**Premise CONFIRMED** — components fall monotonically 53 → 47 (−11.3%),
largest_frac rises, and px rises only +2.6% against that, i.e. fill rather
than halo. No bail; the early exit fires, so **Phase 1 is skipped**.

### F1 — the issue's AC-1 thresholds are STALE

AC 1 asks for "component count materially below 59 and largest-frac above
0.7705", grounded on the PR #2343 measurement of 2026-07-14 (24400 px /
59 comp / 0.7705). **Master today already reads 53 comp / 0.9203 frac** and
therefore clears both thresholds with *zero* change. Master improved the cast
in the intervening 16 days (candidates: #2320/#2387 shadow-throw unification,
#2545/#2562 and #2546/#2576 anchor work). Re-grounded bar: r7 must beat
**today's** r6 baseline, which it does.

The steward confirmed the staleness and ruled that AC 1's operative clause was
always its same-session anchor, not the absolute figures: the splat-off
reference itself moved ~37% in px between sessions (5056 px on 2026-07-14 vs
6936 px today), so no 2026-07-14 number was ever comparable across sessions.
**AC 1: PASS** on the re-grounded same-session A/B.

### F2 — AC-5's byte-identity probe conflicts with the mechanism

AC 5 requires per-axis `yaw30` / `yaw45` byte-identity. Measured with a clean
attribution control:

| suite | result |
|---|---|
| master r6 | 5 of 6 default shots **100.0% / max_delta 0**; only `so3_smooth_sweep` FAILs (max_delta 98 — the documented ~1-in-4 wobble) |
| r7 | **all 6** FAIL at 99.80–99.84%, max_delta 60–64 |

So r7 genuinely changes the rotating shots. The diff image localizes the change
to ~15 scattered **thin-contour clusters, one per world-placed detached solid** —
shadow-*edge* shifts, not dense per-axis content and not a floor-wide acne wash.

Mechanism: `system_bake_sun_shadow_map.hpp` zeroes the radius for the **per-axis
resolve** (`patchSunSplatRadius(0.0f)`), but the **world-placed cast resolve
(P4b-3) deliberately keeps the splat engaged** — its comment states its cast
"has real point-scatter holes the splat must fill (measured)". Every one of
those six shots contains world-placed detached solids, so a radius change
*must* move their cast edges by design.

**RESOLVED (steward direction, 2026-07-30)** — [#2654 comment][sd]. The
world-placed detached cast is **out of scope** for the radius-0 byte-identity
invariant, by citation to text already on master: `sun-shadow-bake-coverage.md`
§ "Byte-identity regimes" invariant 1 names the world-placed cast resolve as
deliberately excluded, and `system_bake_sun_shadow_map.hpp:63-68` scopes the
structural zeroing to the per-axis resolve. Restating AC 5 to cite the *path*
rather than those mixed-content *shots* is a correction, not a re-scope — the
shots were never clean probes of the path they were cited for. Shipping r7
therefore includes re-blessing the affected reference PNGs.

[sd]: https://github.com/jakildev/IrredenEngine/pull/2654#issuecomment-5134594811

## Other acceptance criteria measured at r7

- **AC 2 (acne gate, D5 primary) — PASS.** `--only floor` caster-free floor at
  cardinal: **0 shadow px, hole_ratio 1.0**. No acne reintroduced at r7.
- **`floor_selfshadow` (π/6) — PASS.**
- **`shadow_overlay_floor` structural gate — PASS.**
- **compare_yaw0 / compare_yaw_q — PASS**, and left un-blessed.
- **Cost envelope (#2204 rule) — count-based, and UNMEASURED in wall-clock.**
  r7's atomic cost is 225 writes per caster per cascade against master r6's 169
  (+33%, ×2 cascades), clearing the ratified #2204 ceiling of 289 (the r8
  architect anchor). That rule is a **count** rule, and this PR took no
  frame-time number at either radius — so the envelope is satisfied exactly as
  written, but nothing here establishes the wall-clock delta. A future radius
  change, or any challenge to the count rule itself, needs a real measurement
  rather than this arithmetic. (Raised by the Opus recheck on PR #2654.)
- **AC 7 — Linux smoke owed, and it is a blocking co-requisite.** Reference
  PNGs live in per-preset sibling dirs (`macos-debug/`, `linux-debug/`) because
  Metal and OpenGL are pixel-different. The twinned shader change moves the
  same edges under GL, so `linux-debug/`'s six default-shot references go stale
  too and cannot be regenerated from a macOS pane. A Linux host must discharge
  `fleet:needs-linux-smoke` with `render-verify --update-references` before
  merge, or the GL gate lands red.

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

### A2 — 2026-08-08 — trigger: PR #2654 merged (Phase-0 r7 on master `34c7f7f4`, 2026-08-04)

- **Decision (what shipped, and what this issue still owes).** Phase 0 landed:
  the r6→r7 radius bump in `system_bake_sun_shadow_map.hpp` plus both shader
  twins (`ir_sun_projection.glsl` / `.metal`), the design-doc update, the six
  `macos-debug` `canvas_stress` references, and the AC-4 screenshot set. The PR
  is a **partial** — `[WIP]` in its title, no `Closes #2385` — so this issue is
  correctly still open. Its residual is exactly two items, and neither is the
  code:
  1. **The `linux-debug` re-bless** (AC 7). Still owed — see the next bullet.
  2. **The Phase-1 ruling.** Whether r7 discharges **D6** or #2385 closes as a
     partial with a successor child for the remaining fragmentation (47
     components) is the open question on umbrella #2314, now raised as
     `## STEWARD PROPOSAL 2026-08-08` with `fleet:steward-proposal` applied.
     **Do not decide it by picking this issue up** — the answer determines
     whether Phase 1's encoding widen + radius sweep is in scope at all.
- **Decision (A1's two "still owed before merge" items, resolved).**
  - **AC 4 visual half — DISCHARGED.** A1 required the halo guard on a shot that
    actually shows the delta and named `revoxelize_solids` / `so3_offsnap_wide`
    (`shadow_overlay_floor` measured zero change). The merged PR carries
    before/after/diff for both of those, plus a `revoxelize_solids_floorcrop3x`
    pair, and measures the newly-shadowed rim at 1–2 device px, full shadow
    luminance (85.5 vs deep shadow 77.3 / lit floor 111.5), flat across distance,
    with the pre-existing r6 outermost ring at 88.8. Fill, not halo, on the
    instrument A1 specified.
  - **AC 7 two-host re-bless — NOT discharged.** A1 escalated this from "owed" to
    a **blocking co-requisite**; the PR re-blessed only `macos-debug`. Verified on
    master `28012d3cc`: the six `linux-debug/` `canvas_stress` references were
    last written by `95355bde2` (PR #1595, 2026-06-30) and nothing has touched
    that directory since PR #2654 merged, so the GL reference set is stale as of
    2026-08-04 and a clean-checkout `render-verify --target IRCanvasStress` on a
    Linux host fails its 6-shot gate for reasons unrelated to the change under
    test.
- **The re-bless is NOT this issue's work to schedule.** #1969 (OPEN,
  `human:approved` + `fleet:queued` + `fleet:sonnet` + `fleet:needs-gl-host`)
  already owns the `linux-debug` main-six refresh, and #2158 owns the two
  `compare_*` refs. Blessing at current master discharges r7 as a side effect,
  **provided the bless is taken at master ≥ `34c7f7f4`.** Do not file a duplicate;
  do not fold the re-bless into this issue's scope. Epic-side record: #2314
  ledger **F1** (with F2 recording that the general mechanism failure — a
  deferred acceptance criterion evaporating at merge — is already filed as
  #2530).
- **Supersedes:** A1's "still owed **before merge**" framing for both items —
  the PR merged with only one of them discharged, so AC 7's status is now
  "owed by #1969, tracked at the epic as F1", not "blocking this PR". Nothing
  else in A1 changes: its AC 5 scope decision, its AC 1 re-grounding, and its
  Phase-1 early-exit reading all stand as written.
- **Acceptance criteria:** AC 1 / AC 4 / AC 5 **met** (AC 1 on A1's same-session
  anchor; AC 4 as above; AC 5 as restated by A1). AC 7 **open**, owner #1969.
  AC 2 / AC 3 / AC 6 unchanged. The *closing* criterion for this issue — whether
  the D6 residual is discharged at r7 — is the pending ruling, not a measurement
  a worker can take.
- **By:** epic-steward — source: PR #2654 merge commit `34c7f7f4` (§commit
  message, both squashed commits) and its file list; `git log
  34c7f7f4..origin/master -- creations/demos/canvas_stress/test/references/linux-debug/`
  (empty) and `git log -1 <that dir>` → `95355bde2` / PR #1595; live states of
  #1969, #2158, #2530; epic #2314 ledger D6, D8, F1, F2 and the 2026-08-08 Events
  entry.
