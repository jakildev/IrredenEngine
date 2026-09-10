## Plan: fleet-queue-ingest — retract `fleet:queued` when a planning queue-block lands after queuing

- **Issue:** #2740
- **Model:** opus
- **Date:** 2026-08-08 (plan) / 2026-08-08 (plan review) / 2026-09-10 (implementation)

### Scope

Close the post-queue gap: a planning gate landing on an issue that already
carries `fleet:queued` currently leaves the issue claimable. After this change:
(1) the scout detects the contradiction and the next ingest tick removes
`fleet:queued` (gate labels never touched), and (2) `fleet-claim` refuses to
grant a claim on an issue carrying one, independent of scout-cache freshness.

**Two gates, not three.** The plan and its review were written against
`fleet:needs-plan`, `fleet:plan-review` and `human:review-plan`. PR #3112
retired `human:review-plan` on 2026-09-10 (the issue's own last comment records
the scope change), and `tests/test_fleet_queue_ingest_review_plan_inert.sh` now
pins it as INERT to ingest. Honoring it here would re-arm a retired gate, so the
retract predicate is the two live labels. Both `fleet-claim`'s gate and the
scout's constant carry a test asserting the retired label does *not* fire.

### Verified current state (`origin/master` @ `7567858e5`, probed 2026-09-10)

- #2702's needs-plan+plan-review reconcile and the already-labeled skip guard
  are where the plan review found them; the gap is still live (nothing removes
  `fleet:queued`, and `cmd_claim` referenced none of the gate labels).
- **Live corpus re-swept a fourth time at implementation: 0 issues carry
  `fleet:queued` + a gate, both repos, both arms.** Non-vacuous for the
  `fleet:needs-plan` arm — 33 engine + 2 game issues carry that label, none of
  them queued. The `fleet:plan-review` population is currently 0 in both repos,
  so that arm's zero is trivial rather than evidential. Forward-looking fix, no
  migration pass — confirmed.

### Approach — mirror the `fleet:blocked` unblock round-trip (#1527)

Scout detects → projection edge fires → ingest mutates → set quiesces.

1. **Scout — candidate capture.** `fetch_task_queue` fills a new flat
   `tasks.plan_gated` list of issue numbers from the RAW `fleet:queued` issue
   list, **inside the loop, above the section `continue`s**.
2. **Scout — candidates + projection.** `_ingest_retract_candidates` beside
   `_ingest_unblock_candidates`; `{"op": "retract"}` items in
   `project_queue_manager_ingest` (the hash edge); `retract_issues` in
   `slice_queue_manager_ingest`.
3. **Ingest — reachability.** Fast-path empty-projection exit counts
   `retract_issues` too.
4. **Ingest — retract pass**, modeled on the unblock pass: live
   `gh issue view --json labels`, act only when `fleet:queued` AND at least one
   gate are BOTH still present, remove `fleet:queued` only, post one terse
   comment naming the gate. Counters `retracted` / `retract_skipped` wired
   through all three positionally-coupled surfaces.
5. **fleet-claim — belt-and-braces.** `check_planning_gates()` modeled on
   `check_model_tag`, called in `cmd_claim` (which covers `--stackable-on`) and
   in the `stack` path's per-issue validation loop.

### How the plan review's three binding constraints are discharged

**1. `tasks.open` is the wrong candidate source.** Satisfied by construction:
the capture is `tasks.plan_gated`, filled above `fetch_task_queue`'s
`continue`s, so the `fleet:plan-review` arm — dropped before a task row exists —
is captured. `tests/test_scout_task_queue_plan_gated.py` pins this directly: the
plan-review case asserts the issue is in `plan_gated` **and** that `open` and
`in_progress` are both empty, so a refactor that moves the capture below a
`continue` fails there rather than going quietly half-blind.

**2. Claimed issues land in `tasks.in_progress`.** Also satisfied by
construction — the capture predates the section split entirely, so owner is
never consulted. The plan's "no owner filter, retracting under an active claim
is intended" gotcha is therefore deliverable and kept. Pinned by
`test_claimed_gated_row_is_captured` (asserts `plan_gated` **and**
`in_progress`) and by the ingest suite's #782 case (claim label untouched).

**3. The acceptance criteria were vacuous over exactly the dead arms.** Both
prescribed positive-fire cases exist — #781 (`fleet:plan-review`, free) and #782
(gate + `fleet:claim-mac-pool-3`) — and every new suite is negative-controlled
against the pre-fix ref with `fleet-positive-control` (results in the PR body).

### Deviations from the plan, found while implementing

- **The scope-shipped pre-flight could skip the retract pass entirely.** Ingest
  exits 0 when every pending issue has merged coverage
  (`kept_count == 0 && shipped_count > 0`). That guard predates both the
  unblock and retract passes, which read their own projection lists and have
  nothing to do with scope-shipped coverage, so a run mixing scope-shipped
  pending work with retract (or unblock) work silently skipped it. The exit now
  additionally requires both other work lists to be empty. This is the same
  reachability class as the issue's own "reconcile must be reachable"
  criterion, one guard further up; the unblock pass had been exposed to it
  since #1527.
- **The stamping block's error-path summary print had the wrong arity** (4
  fields against a 9-field `IFS=,` read). Benign — the read leaves the extra
  vars empty and every consumer is `${x:-0}` — but it is exactly the trap the
  plan's "three positionally-coupled counter surfaces" gotcha names, so it is
  now derived from the field count with a comment saying so.

### Files

- `scripts/fleet/fleet-state-scout` — `_PLAN_GATE_LABELS`,
  `_RETRACT_PARK_LABELS`, `tasks.plan_gated` capture,
  `_ingest_retract_candidates`, projector + slicer entries, degraded-fetch
  default.
- `scripts/fleet/fleet-queue-ingest` — header, fast-path count, scope-shipped
  exit guard, retract pass, counter wiring ×3.
- `scripts/fleet/fleet-claim` — `check_planning_gates()`; calls in `cmd_claim`
  and the stack validation loop.
- `scripts/fleet/tests/test_fleet_queue_ingest_retract_gate.sh` (new),
  `scripts/fleet/tests/test_scout_task_queue_plan_gated.py` (new),
  `scripts/fleet/tests/test_fleet_claim_planning_gate.sh` (new),
  `scripts/fleet/tests/test_queue_manager_projection.py` (extended).
- `docs/agents/fleet-labels-reference.md` — the two gates are now *retracting*
  gates.

### Gotchas honored

- **Never remove the gate labels.** Retract removes `fleet:queued` exclusively.
- **Parks excluded from the candidate set.** `fleet:needs-human` / `fleet:gated`
  already hold an issue out of `tasks.open` unconditionally, so there is no
  pickup defect to retract; excluding them keeps their handling literally
  unchanged (an issue acceptance criterion) and spends no API budget.
- **The closed loop after retract:** `human:approved` + gate, no `fleet:queued`
  → `_INGEST_SKIP_LABELS` holds it out of `pending_issues` (parked, correct).
  Gate cleared → membership change → hash flip → normal re-queue.
- **`planning-claim` is untouched** — it deliberately targets `fleet:needs-plan`
  issues and does not route through `cmd_claim`.
- **Engine-repo change, both repos covered:** scout/ingest iterate engine + game
  via `REPO_SLUGS`.
