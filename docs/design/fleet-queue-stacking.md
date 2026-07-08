# Fleet queue stacking — design

Source-of-truth model for how the fleet queues blocked tasks and feeds the
autonomous stacking path. Supersedes the earlier "queue one child at a time"
behavior (#1476) with a **queue-all / mark-blocked** model (#1527).

Companion to [`../agents/FLEET.md`](../agents/FLEET.md) (the operational
workflow) and [`../agents/fleet-labels-reference.md`](../agents/fleet-labels-reference.md)
(per-label ownership). This doc owns the *why* and the lifecycle.

## Problem

A stacked epic decomposes into a dependency chain — child *B* is
`**Blocked by:** #A`, *C* by *B*, and so on. Two behaviors are in tension:

1. **Visibility.** An operator (and `fleet-queue-list`) should see the whole
   approved backlog, not just the one workable head. Under the old model a
   ten-ticket epic showed a single queued issue; the other nine were invisible
   until each predecessor closed, so the shape of the remaining work was
   impossible to read from the queue.
2. **Correct claimability.** A worker must not *plainly* claim a task whose
   predecessor hasn't merged — its diff would be built on the wrong base. But a
   worker that wants to *stack* (open a PR based on the blocker's open branch)
   must be able to, when context makes it worth the coupling.

The old model resolved the tension by keeping blocked children out of the
queue entirely (`fleet:queued` was withheld until every blocker closed). That
made claimability trivially correct but sacrificed visibility, and it meant the
stacking-enrichment pass (`stackable_blocker_pr`) had nothing to enrich for
engine epics — a blocked child wasn't in `tasks.open[]` to carry the field.

## Model: queue-all, mark-blocked

Ingestion queues **every** approved, non-skip task regardless of blocked
state. A task whose `**Blocked by:** #N` predecessor is still open is queued
with an extra `fleet:blocked` marker.

| State | Labels | In `tasks.open[]`? | Plain claim | `--stackable-on` |
|---|---|---|---|---|
| approved, unblocked | `fleet:queued` + model | yes, `blocked:false` | ✅ granted | ✅ |
| approved, blocked | `fleet:queued` + model + `fleet:blocked` | yes, `blocked:true` | ❌ refused | ✅ (against blocker's open PR) |
| skip (epic / needs-plan / needs-info / needs-human / scope-shipped) | — | no | — | — |

The `fleet:blocked` label is the **visible** half of the contract; it does not
itself gate claims. Claimability is enforced independently by `fleet-claim`'s
own `**Blocked by:**` parse, which is unchanged — so a blocked task is queued
and visible, yet a plain `fleet-claim claim` against it is still refused
(`fleet-claim claim <N> <agent> --stackable-on <PR>` succeeds because that path
deliberately bases the new branch on the blocker's open branch).

## Lifecycle

```
file issue ──(human:approved)──▶ ingest pass
                                    │
              blocker open? ── yes ─┼─▶ + fleet:queued, model, fleet:blocked
                                    │       │
                                    no      │  (last blocker closes)
                                    │       ▼
                                    │   unblock pass ──▶ − fleet:blocked
                                    ▼       │
                              + fleet:queued, model   ◀──┘
                                    │
                              worker claims ──▶ fleet:in-progress ──▶ PR ──▶ merged ──▶ closed
```

Both transitions are **edge-triggered** off the scout's
`queue-manager-ingest` projection hash, which fires `fleet-queue-ingest`:

- **Add path** — a newly `human:approved` task enters the projection (sourced
  from `human_approved[]`, which excludes `fleet:queued`). Ingest stamps
  `fleet:queued` + model, plus `fleet:blocked` if a live `gh issue view`
  recheck finds an open predecessor. The task then carries `fleet:queued` and
  drops out of `human_approved[]`.
- **Remove path** — once queued, a blocked task has left `human_approved[]`, so
  the removal can't be sourced there. It is sourced instead from
  `tasks.open[]` (the `fleet:queued` surface): the scout's
  `_ingest_unblock_candidates` yields any open task that carries `fleet:blocked`
  but whose `resolve_blocked_by` has reduced `blocked_by` to `(none)` — i.e.
  the last blocker just closed. That candidate enters the projection (the hash
  flips), ingest re-fires, live-rechecks the blockers, and removes
  `fleet:blocked`. The next tick the candidate is gone (label cleared) and the
  hash settles — the same "one wasted no-op iteration" the add path has always
  had.

The **live `gh issue view <ref> --json state` recheck** in `fleet-queue-ingest`
is authoritative for both add and remove. The scout's in-memory `blocked` flags
(`human_approved[].blocked`, `tasks.open[].blocked`) only decide *when the
trigger fires*; the label is applied or cleared only after the live recheck
agrees. This keeps the mechanism correct even when ingest runs against a
hand-built projection (tests, a manual invocation).

## Stacking enrichment

Because blocked children now appear in `tasks.open[]`, the
`enrich_stackable_blocker_prs` pass can attach a `stackable_blocker_pr` field to
a single-blocker task when the blocker has exactly one matching open
`claude/<N>-*` PR (and the two filters pass — no design-block on the blocker,
no file-area overlap). A worker with no plainly-claimable task falls through to
the stackable tier and claims `--stackable-on <PR>`. This is the autonomous
"feed stacking" half: the queue surfaces a blocked task *and* the PR it can
stack on, in one record.

Per-repo note: worker pickup honors `stackable_blocker_pr` for tasks in **all
repos** (engine and game) — the merger's cascade-rebase processes stacked PRs in
both the engine pass and the game pass. A `repo == game` stackable task is
claimed with `--repo game`. Multi-blocker tasks are not eligible for stacking in
v1 (`_single_blocker_issue` returns nothing for them).

## Why reconcile leaves these alone

A `fleet:queued` + `fleet:blocked` task with no claim and no PR matches **no**
`fleet-claim reconcile` rule: R4b requires `fleet:in-progress`, R6 requires
`human:owned`, and R2/R7 require an open PR. So a queued-blocked task sits
quietly in the queue — visible, not claimable, not swept — until either a
worker stacks on it or its blocker closes and the unblock pass clears the
marker.

## Acceptance invariants

- A blocked, approved, non-skip task → `fleet:queued` + model + `fleet:blocked`;
  an unblocked one → `fleet:queued` + model only.
- The task appears in `tasks.open[]` with `blocked: true`; with a single open
  blocker PR it also carries `stackable_blocker_pr`.
- `fleet-claim claim <blocked-N> <agent>` is refused; `… --stackable-on <PR>`
  succeeds.
- When the last blocker closes, the next ingest pass removes `fleet:blocked`.
- Reconcile/cleanup never sweeps a `fleet:queued` + `fleet:blocked` no-claim
  issue.

## Maintenance gotchas (worker ↔ merger contract)

Incident rationale behind the stacked-PR maintenance steps in
`role-worker.md` (step 1c) and `role-merger.md` (step 5a.5). The role docs
keep the commands; this section keeps the *why*.

### Inherited-prefix drop after the parent merges (#1791)

When a stacked child conflicts against master purely because it still
carries a stale *inherited* copy of files from a parent PR that has since
merged, the resolution is topological, not textual: replay only the
child's own commits with `git rebase --onto origin/master
<child-fork-point>`, where the fork point is `git merge-base HEAD
origin/<parent-branch>` (the parent's *pre-merge* head). Inherited files
then resolve to master and the diff shrinks to the child's own changes.

`fleet-rebase` normally performs this drop automatically by checking the
parent PR's recorded `headRefOid` against the child's ancestry. It
silently doesn't fire when the parent was **amended during review after
the child forked** — the recorded `headRefOid` is no longer an ancestor
of the child's head, so the ancestor check fails and the tool falls back
to a plain rebase that replays the whole inherited prefix as conflicts
(#1791). That is why role-worker step 1c d' instructs the manual `--onto`
drop whenever every conflicted file is inherited.

## References

- #1527 (this mechanism), #1526 (umbrella epic), #1476 (the queue-one-at-a-time
  behavior this revises), #1521 (`(none)` / bare-none Blocked-by parser).
- `fleet-queue-ingest` (add + unblock passes), `fleet-state-scout`
  (`_ingest_unblock_candidates`, `project_queue_manager_ingest`,
  `slice_queue_manager_ingest`, `enrich_stackable_blocker_prs`,
  `fetch_task_queue`'s `blocked` flag).
