# Plan — issue #2011

- **Issue:** fleet: architect file-with-plan default (skip worker re-plan) + human:review-plan gate for planless high-stakes
- **Model:** opus
- **Date:** 2026-06-24

## Scope
Make architect-filed-with-plan the default for substantial single tasks (leveraging the existing `## Plan`-comment skip in the ingest planning gate), and add a `human:review-plan` gate for high-stakes worker-planned (planless-filed) issues. Pure fleet-infra: protocol docs + two scripts + the label catalog. No engine/game source changes.

## Approach
1. **`docs/agents/TASK-FILING.md`** — add a "file with a plan" path for single tasks: when the task was planned with the human (or is substantial), post the structured `## Plan` comment at file time (point to PLANNING-PROTOCOL.md §plan structure). Clarify that this skips the planning gate (queues directly), and that plan-less filing remains valid for mechanical tasks / `human:no-plan` spikes. Today only `file-epic` posts `## Plan` comments — generalize the guidance to single tasks.
2. **`docs/agents/architect-protocol.md`** (§Filing tasks) — make it explicit: if you planned a task with the human, **post the `## Plan` comment when you file it** (don't leave the plan in the body), so it skips worker re-planning and the human isn't re-looped. Cross-link the dogfood rationale.
3. **`docs/agents/PLANNING-PROTOCOL.md`** — document the `human:review-plan` gate for the worker path: after a worker posts a `## Plan` for a **high-stakes** `fleet:needs-plan` issue, it adds `human:review-plan` (hold) alongside `fleet:plan-review`; a human clears it → queues. Define "high-stakes" via an explicit checklist (ambiguous approach / cross-cutting / expensive or hard to reverse / public-contract change). Low-stakes → queue as today.
4. **`docs/agents/fleet-labels-reference.md`** — add `human:review-plan` (human-owned): applied to a worker-planned high-stakes issue to hold it for human approach-sign-off before implementation; human removes it to release to queue. Note the distinction from `fleet:plan-review` (agent vetting).
5. **`scripts/fleet/fleet-queue-ingest`** — (a) confirm/keep: an approved issue with a `## Plan` comment skips `fleet:needs-plan` and queues; add a comment referencing the architect-file-with-plan flow. (b) treat `human:review-plan` as a queue-block (same as `fleet:needs-plan`/`fleet:plan-review`): do not stamp `fleet:queued` while present.
6. **`scripts/fleet/fleet-state-scout`** — surface `human:review-plan` issues in the human projection (`repos.<repo>.review_plan`) so the human knows to review, and add it to `_INGEST_SKIP_LABELS` so it is never re-queued.
7. **`.claude/commands/role-worker.md`** (§planning) — after posting a `## Plan` for a needs-plan issue, if high-stakes, add `human:review-plan` instead of swapping straight toward queue; document the high-stakes test. NOTE: this is gated self-config a queue-sourced worker cannot apply — the behavior is fully specified in PLANNING-PROTOCOL.md step 3 (which role-worker's planning step delegates to). Deferred to the human (see PR note).
8. **`docs/agents/FLEET.md`** + **`docs/agents/fleet-state-machine.json`** — update the planning-gate narrative + label state machine to show both paths: architect-file-with-plan → queue; planless → needs-plan → (worker plan) → [high-stakes: human:review-plan] → queue. Add the `human:review-plan` label + `plan-propose-high-stakes` / `plan-human-approve` transitions.

Also: add `human:review-plan` to the `scripts/fleet/fleet-labels` catalog (≤100-char GitHub-synced description) so the label syncs to both repos.

## Affected files
- `docs/agents/TASK-FILING.md`
- `docs/agents/architect-protocol.md`
- `docs/agents/PLANNING-PROTOCOL.md`
- `docs/agents/fleet-labels-reference.md`
- `docs/agents/FLEET.md`
- `docs/agents/fleet-state-machine.json`
- `scripts/fleet/fleet-queue-ingest`
- `scripts/fleet/fleet-state-scout`
- `scripts/fleet/fleet-labels`
- `scripts/fleet/tests/test_fleet_queue_ingest_review_plan.sh` (new test)
- `.claude/commands/role-worker.md` — gated; deferred to human (see PR note)
- (create the `human:review-plan` GitHub label on both jakildev/IrredenEngine and jakildev/irreden — done via `fleet-labels`)

## Acceptance criteria
- An architect-filed issue with a `## Plan` comment is queued by ingest WITHOUT passing through `fleet:needs-plan` (covered by `test_fleet_queue_ingest_plan_gate.sh`'s plan-comment skip + the dogfood of this very issue).
- An issue carrying `human:review-plan` is NOT stamped `fleet:queued` until the label is removed (`test_fleet_queue_ingest_review_plan.sh`).
- A planless high-stakes issue, after worker planning, lands on `human:review-plan`; a planless low-stakes issue queues without it.
- Docs across FLEET.md / architect-protocol.md / TASK-FILING.md / PLANNING-PROTOCOL.md / fleet-labels-reference.md describe one consistent two-path flow; the game architect inherits it via the shared protocol (no game-repo edit needed).

## Gotchas
- `human:review-plan` must survive re-ingest (mirror the `fleet:needs-human` pattern that keeps `human:approved` so the issue isn't dropped) — it is NOT in `_ALREADY_QUEUED_LABELS`, so `fetch_human_approved` still returns it, and `_INGEST_SKIP_LABELS` keeps it out of the pending set.
- Don't break the existing `## Plan`-comment skip or the `human:no-plan` opt-out — extend, don't replace.
- "High-stakes" needs a crisp, checkable definition — use the explicit four-item checklist over vibes.
- The ingest already keys on the `## Plan` comment for BOTH repos via `--repo game`.
- The `fleet-labels` GitHub-synced description must stay ≤100 chars (the full prose lives in `fleet-labels-reference.md` / `fleet-state-machine.json`, which are doc-only and not length-capped).
