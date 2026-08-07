# Plan: fleet-claim R7 re-arms fleet:design-unblocked on infra-blocked orphaned WIP PRs → opus-dispatch churn

- **Issue:** #2462
- **Model:** opus — bounded bash/python + tests once planned, but reconcile-rule
  and label-protocol semantics need fleet-protocol judgment; not sonnet-mechanical,
  no novel algorithm to warrant fable
- **Date:** 2026-07-20 (planning pass); amended 2026-08-07 at implementation

## The defect

R7 (`reconcile_heal_design_unblock`) re-adds `fleet:design-unblocked` to a
claimless `fleet:wip` PR whenever its backing issue is `fleet:queued` and the PR
carries none of the three design labels. The predicate never consults whether the
work is *actionable*, so a PR parked behind an unlanded blocker re-arms forever.
Each re-arm routes to an opus dispatch (`fleet_task_class.feedback_pr_class` maps
`fleet:design-unblocked` → opus) that can make zero progress.

Three PRs have carried this: #2460 (issue #2260), #2393 (issue #2321), and one
PR in a downstream repo with the same shape. #2393 alone has absorbed 7 opus
feedback pickups.

## Amendments folded in from the issue thread

The 2026-07-20 plan is the base. Four later comments changed it:

1. **2026-07-28 (pool-3) — add a `fleet:blocked` predicate.** Both live cases
   have a backing issue that is *already* `fleet:blocked`; R7 gates only on
   `fleet:queued` and never tests it. This needs no new label, no body marker,
   and no worker action — the blocked state is already recorded by the existing
   protocol. Folded in as step 4 below.
2. **2026-07-31 (pool-3) — candidate fix 3 (per-PR re-heal cap) is refuted.**
   `design-unblock-heal-persistence.json` is reset-on-success by construction
   (`json.dump(seen, ...)` retains only keys found *this* run), so a cap built on
   it always reads zero at the moment it would need to say "already healed once".
   Not pursued.
3. **2026-08-01 (pool-3) — the worker's clear is what drives the churn.**
   Dispatch is edge-triggered on a projection hash, so leaving the label costs
   **zero** wakes; clearing it costs one, and R7's re-add costs another. R7's
   finding condition (`neither design label`) only reproduces *after* a clear.
   This reclassifies the change: it is a **correctness** fix that makes a
   worker's clear terminal, not a churn-rate fix.
4. **2026-08-07 (pool-4) — why this never queued.** #2856's ingest window, not
   a labelling problem. Out of scope here.

## Scope

Two disjoint populations, two mechanisms:

| population | mechanism |
|---|---|
| backing issue carries `fleet:blocked` (#2393/#2321, plus the downstream pair) | R7 predicate skip — zero worker action |
| blocked on infra with no `Blocked by:` field (#2460/#2260 behind a build wall) | explicit `fleet:awaiting-infra` park + `Parked-until: #N` |

Plus a new reconcile rule **R8** that auto-un-parks when the named blocker
closes, after which the existing R7 machinery re-surfaces the PR organically.
Genuine #1516 stranding (no marker, unblocked issue) keeps healing exactly as
today.

## Approach

1. **New label `fleet:awaiting-infra`** via the 3-file sync, one commit:
   - `scripts/fleet/fleet-labels` catalog entry (description ≤100 chars — the
     `--check` gate enforces it).
   - `docs/agents/fleet-state-machine.json` node-set entry.
   - `docs/agents/fleet-labels-reference.md` semantics + park/un-park protocol.

   `./scripts/fleet/fleet-labels --check` must pass. The live label is created
   in both repos by the normal `fleet-labels` sync on the next `fleet-up`.

2. **Park protocol** (documented in `docs/agents/FLEET-FEEDBACK-HANDLING.md` and
   the labels reference — both un-gated `docs/agents/` files; no gated
   self-config in scope). When a worker adjudicates an orphaned `fleet:wip` PR
   as complete-but-blocked-on-infra:
   - remove `fleet:design-unblocked` if present + add `fleet:awaiting-infra`;
   - append `Parked-until: #<blocker-issue>` to the **PR body** (same-repo
     issue; last occurrence wins, so a re-park just appends);
   - comment the rationale on the PR.

   Bare removal of `fleet:design-unblocked` *without* a marker and *without* a
   `fleet:blocked` backing issue intentionally stays re-heal-able: R7 cannot
   distinguish a deliberate bare un-set from a crashed worker's TTL-swept claim,
   and re-arming the latter is its job. The marker **is** the deliberate-removal
   signal.

3. **R7 + R2 exemption for `fleet:awaiting-infra`** (`scripts/fleet/fleet-claim`):
   add it to the design-label exemption test in the R7 finding loop and to R2's
   equivalent skip. Mirrors how `fleet:design-proposed` is already invisible to
   both (#1663), with a comment citing #2462.

4. **R7 `fleet:blocked` predicate** (the 07-28 amendment): skip the R7 finding
   when the backing issue carries `fleet:blocked`. Under the queue-all model
   (#1527) a blocked task legitimately keeps `fleet:queued`, so this state is
   protocol-correct and there is by definition nothing for the worker resume
   loop to act on.

   **R2 deliberately keeps flagging it.** R2 is flag-only and dedupes into a
   single state-drift tracker; an orphaned WIP on a blocked issue is still worth
   a human's eye, and narrowing R2 here is beyond both the plan and the
   amendment. Only the *auto-mutating* rule is narrowed.

5. **New rule R8 (auto-un-park)** — detection in the same findings pass + an
   apply-mode function `reconcile_unpark_infra` wired in `cmd_reconcile`:
   - *Detection:* iterate the flat `d["prs"]` list (not `pr_by_issue` — a parked
     PR may lack a resolvable backing-issue mapping, and `pr_by_issue` entries
     carry no `body`). For each open PR with `fleet:awaiting-infra`:
     - last `Parked-until: #(\d+)` in `body` parses → emit an R8 finding **with**
       an apply action. Apply-carrying findings are skipped by
       `reconcile_escalate_drift`'s flag-only accrual, so a long-parked PR files
       no drift tracker.
     - no parse → emit a **flag-only** R8 finding so the normal drift-tracker
       accrual escalates a malformed park to a human.
   - *Apply* (`--apply` only): verify the blocker live via `gh issue view <N>
     --json state`. `CLOSED` → remove `fleet:awaiting-infra`. Anything else
     (OPEN, lookup failure) → no-op; the finding recurs harmlessly.
     **No persistence gate**: closing an issue is a deliberate durable act, and
     the bad case (blocker reopened after un-park) self-corrects — R7 re-arms, a
     worker re-adjudicates and re-parks.
   - Reconcile never edits PR bodies; stale earlier `Parked-until:` lines are
     inert because the parse takes the last occurrence.
   - *Composition:* after un-park the PR reverts to the stranded state; R7
     re-arms after the usual threshold; the worker resume loop adopts it. No new
     resume machinery.

6. **Tests** — extend
   `scripts/fleet/tests/test_fleet_claim_reconcile_heal_design_unblock.sh`:
   - *Parked-invisible:* `fleet:awaiting-infra` PR with `Parked-until: #N`
     (blocker OPEN) → no R7 and no R2 finding across ≥3 ticks; R8
     apply-carrying finding present; no label mutation.
   - *Un-park fires (positive-fire):* same state, blocker canned CLOSED → one
     `--apply` tick removes `fleet:awaiting-infra` (exactly one recorded label
     removal); then flip the PR marker-less and confirm R7 resumes and heals at
     threshold — the full un-park → re-surface pipeline.
   - *Malformed park:* label present, no `Parked-until:` line → flag-only R8
     finding, no mutation.
   - *`fleet:blocked` backing issue:* claimless `fleet:wip` PR, neither design
     label, issue `fleet:queued` + `fleet:blocked` → no R7 across ≥3 ticks and
     no heal, while the unblocked control on the same tick still heals.
   - *Regression:* every existing phase stays green.

## Affected files

- `scripts/fleet/fleet-claim` — R7/R2 exemptions; R7 `fleet:blocked` predicate;
  R8 detection; `reconcile_unpark_infra`; `cmd_reconcile` wiring.
- `scripts/fleet/fleet-labels` — catalog entry.
- `docs/agents/fleet-state-machine.json` — node-set entry.
- `docs/agents/fleet-labels-reference.md` — label semantics + park/un-park protocol.
- `docs/agents/FLEET-FEEDBACK-HANDLING.md` — the park branch on the
  design-unblocked path.
- `scripts/fleet/tests/test_fleet_claim_reconcile_heal_design_unblock.sh` — new phases.

## Acceptance criteria

1. A parked PR (per protocol, blocker OPEN) produces **no** R7 heal and **no**
   R2 flag across ≥ threshold `--apply` ticks.
2. **Positive-fire:** with the blocker CLOSED, one `--apply` tick observably
   removes `fleet:awaiting-infra`, and R7 subsequently re-heals at threshold. The
   OFF-path quiet alone does not pass.
3. A `fleet:queued` + `fleet:blocked` backing issue suppresses R7 across ≥
   threshold ticks, while an unblocked control on the same ticks still heals.
4. Genuine #1516 case (marker-less, unblocked stranded WIP) still heals at
   threshold — existing phases unchanged and green.
5. Malformed park escalates via the flag-only → state-drift-tracker path.
6. `./scripts/fleet/fleet-labels --check` green; whole test file green;
   verifiable on any host (bash/python only, no engine build).

## Gotchas

- The heal-persistence file resets keys absent this run — do **not** reuse it as
  heal history; the exemption must come from the label or the issue's own state
  (cross-host), never local state.
- R2's exemption site is separate from R7's; miss one and a parked PR keeps
  accruing toward state-drift trackers.
- `gh issue edit` on a PR number is the file-wide convention for PR label edits
  (labels share the issues endpoint) — don't switch to `gh pr edit` inside
  `fleet-claim`.
- Catalog description hard cap: 100 chars (`fleet-labels --check` enforces).
- `Parked-until:` is same-repo-only in v1 (documented); the motivating
  #2460 → #2449 case is same-repo.
- Post-un-park there is a ~threshold-tick latency before R7 re-arms — by design
  (reuses the tested freshly-propagating-claim protection); do not shortcut it by
  having R8 add `fleet:design-unblocked` directly.
- Everything the worker needs lives in `scripts/fleet/` + `docs/agents/` — do
  **not** touch gated role/skill docs for this.

## Task shape

One task, one PR — no stack split. This plan file lands as the first commit of
the implementation PR (#1932).
