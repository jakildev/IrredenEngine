## Plan: fleet/planning: park an unplannable needs-plan issue with `fleet:needs-human`

- **Issue:** #3034
- **Model:** opus
- **Date:** 2026-08-22 (plan) / 2026-09-06 (implementation)

### Scope

Give the planner a sanctioned terminal state for an **unplannable** needs-plan issue (premise refuted, target code absent, direction disagreement that needs a human routing decision), and make the two fleet consumers that currently ignore it — the scout's planning projection and `fleet-claim planning-claim` — honor it, so the head-of-lane unplannable issue stops being re-dispatched every tick. No new label: widen the existing park, `fleet:needs-human`, whose documented meaning ("fleet can't do it autonomously, human must act; keeps `human:approved`") already fits and which three of the four park sets already include.

### Verified current state (read 2026-08-22 against `origin/master` 816efbd5; re-confirmed 2026-09-06 against `53973faa`)

- `scripts/fleet/fleet-state-scout`: `_ALREADY_QUEUED_LABELS` and `_INGEST_SKIP_LABELS` both contain `fleet:needs-human`; `_HUMAN_GATE_LABELS` (l.491) = `{human:owned, human:wip, human:no-plan, fleet:no-plan}` does not. The set has **four** consumers, all issue-side: `project_worker` (l.2116), the plan-review trigger loop (l.2201), `slice_worker` (l.2621), and `slice_opus_reviewer`'s plan_review (l.2684).
- `scripts/fleet/fleet-claim` `cmd_planning_claim`: gates = `_refuse_if_closed`, the `--replan` needs-plan guard (rc 2), and the `## Plan` dedup (rc 3). No label-based refusal.
- `scripts/fleet/fleet-dispatcher` `plan_assign_for_pane`: walks `--plan-pick` lines in order; rc 0 assigns, rc 3 retries `--replan`, **everything else falls through to the next line**. So an exit-1 refusal needs no dispatcher change, and 1 is the only free code (2 and 3 are taken).
- `fleet_task_class.plan_pick` sources `needs_plan[]` from the **slice** (`projections/<role>.json`, i.e. `slice_worker` output), so gating the set genuinely reaches the dispatcher's walk rather than being decorative.
- `docs/agents/PLANNING-PROTOCOL.md` step 2 and the "If you disagree" branch both end at "leave `fleet:needs-plan` on" — neither names a park.
- Fired incident: game #94, 13 planning dispatches to the same verdict, then **2 more after the park was hand-applied** (2026-08-22T23:19Z, ~23:55Z) — the label-widening half is measured as load-bearing, not reasoned.

### Approach (one approach, picked)

1. **Scout — honor the park in the planning projection.** Add `"fleet:needs-human"` to `_HUMAN_GATE_LABELS` and extend the set's comment. All four consumers read the set, so no other scout code changes. `fetch_needs_plan` is deliberately left alone: `state.json` stays the full cross-host record and the projections do the filtering (the smoke lane's shape). `_INGEST_SKIP_LABELS` / `_ALREADY_QUEUED_LABELS` already include the label; `_INGEST_OVERRIDE_LABELS` is untouched, so `human:revise-plan` stays ingest-side and cannot un-park the planner projection.
2. **`fleet-claim` — refuse a parked issue (backstop).** In `cmd_planning_claim`, after `_refuse_if_closed` and before the `--replan` / dedup split, refuse with exit 1 when the issue carries `fleet:needs-human`. Applies to **both** branches: a re-plan of a parked issue is still unplannable.
3. **`fleet-claim` — refuse a stale first-plan candidate.** Amendment from the 14th dispatch (thread, 2026-08-23): the first-plan path never checked that `fleet:needs-plan` is present *at all* — only `--replan` did — so the dispatcher pre-claimed an issue whose flag had been cleared between the projection it walked and the claim, and only the worker's own step-2 re-check caught it, a full opus dispatch later. Refuse (exit 1) before the dedup probe, so the already-planned-but-unflagged shape exits here rather than exiting 3 and provoking a `--replan` retry the guard in step 2 would refuse anyway.
4. **One probe, per-gate failure policy.** Both new gates and the existing `--replan` guard share `_issue_label_probe`, a tri-state (`true` / `false` / `unknown`) reader over one `gh issue view --json labels` call shape. `unknown` is a distinct answer so each gate picks its own failure direction at the call site: the two new gates fail **open** (a gh outage must not wedge every planning claim on the host), while `_issue_has_needs_plan` keeps its existing fail-**closed** behaviour for `--replan`, which is an explicit operator request and safe to make the caller retry.
5. **PLANNING-PROTOCOL.md — name the park.** In step 2 "One approach, picked", distinguish "needs more planning thought" (no park; a later planner may succeed) from "no planner can resolve this without a human action" (park), and state the re-entry contract. Same park in the "If you disagree with the issue's direction" branch.
6. **fleet-labels-reference.md § `fleet:needs-human`.** Split the entry into its execution lane and its new planning lane, name the two consumers that now honor it, and record the `fleet:plan-review` interaction.
7. **No retro-apply.** The next planning dispatch that lands on a parked issue applies the new rule; this PR edits no other issue's labels.

Rejected alternative: a new `fleet:needs-routing` label — needs creation in both repos, a reference entry, and a fourth park set to keep in sync, to express a meaning `fleet:needs-human` already carries.

### Affected files

- `scripts/fleet/fleet-state-scout` — `_HUMAN_GATE_LABELS` + comment
- `scripts/fleet/fleet-claim` — `_issue_label_probe`, the park gate, the stale-candidate gate
- `scripts/fleet/tests/test_fleet_claim_planning.sh` — T11–T14; the `gh` stub now evaluates the `--jq` program against a fixture label set
- `scripts/fleet/tests/test_worker_projection.py`, `scripts/fleet/tests/test_opus_reviewer_projection.py` — park cases on all four consumers
- `docs/agents/PLANNING-PROTOCOL.md`, `docs/agents/fleet-labels-reference.md`

### Acceptance criteria (positive-fire)

Every new assertion was run against `origin/master` source with the post-fix suites overlaid (the `fleet-positive-control` recipe) and **fails there**:

- `test_worker_projection.py` — `[94] != []` (project_worker) and `[94, 222] != [222]` (slice_worker).
- `test_opus_reviewer_projection.py` — hash mismatch on the held-label loop, and the parked issue present in `slice_opus_reviewer`'s `plan_review`.
- `test_fleet_claim_planning.sh` — T11/T12 acquire instead of refusing; T13 exits 3 (the dedup) instead of 1.

Plus: `bash scripts/fleet/tests/run_all.sh` green, `ruff check scripts/` clean.

### Gotchas

- **#2768:** `fleet-state-scout` and `fleet-dispatcher` are persistent daemons; the merged change is inert on a running host until `fleet-down` / `fleet-up`.
- `role-worker.md` step 2 points at PLANNING-PROTOCOL.md and is gated self-config — keeping the park instruction in the protocol doc avoids a gated edit.
- `_HUMAN_GATE_LABELS` is also the review-projection hold set, so a `fleet:plan-review` issue parked with `fleet:needs-human` drops from plan-review too. Intended, asserted, and documented in both the set's comment and the labels reference — the pool-5 thread note asked for this to be an explicit line rather than a discovered side effect.
- The architect is the other legitimate `planning-claim` caller and is refused too. That is correct: label removal *is* the un-park, and forcing the routing decision to be explicit is the point. Unlike the `amending-claim` / `fleet:needs-gl-host` precedent (a host-capability gate, un-overridable by nature), this one gates on a human-clearable park — the precedent transfers on shape, not on the override question.
- Tests are hermetic; the `gh` stub models the tool's argument parsing rather than pre-baking its answer, because three gates now share the `--json labels` call shape.
