# Plan: a `Blocked by: #<PR>` ref is invisible to the stacking surfaces

- **Issue:** #2523
- **Model:** opus — bounded script change, but the fail-closed semantics and the
  #1751 offer/accept-agreement invariant need judgment beyond mechanical execution
- **Date:** 2026-07-23 (planned), amended 2026-08-01 at implementation

## Scope

Direction **(a)** from the issue body: make a `Blocked by: #<PR>` ref (a PR
number with no backing issue) first-class on the stacking surfaces — the scout's
offer (`enrich_stackable_blocker_prs`) and the live finder
(`cmd_find_stackable_blockers`) — so a task is stackable on its blocker PR while
that PR is open. Direction (b) (reject the form at filing time) is rejected: it
re-creates the #1749 unparsed-prose failure mode and leaves the genuine "wait for
this approved-but-unmerged PR" dependency inexpressible. (a) preserves the
queue-all + stacking model (#1527).

## Amendment A1 (2026-08-01) — Surface 2 was retired before implementation

The plan as approved covered **three** surfaces. Surface 2 (the claim's
`--stackable-on` ancestry gate) no longer exists: PR #2656
(`fleet: retire self-built stack mechanics + labels`) **merged**, deleting
`missing_ancestor_reason()` from `fleet_stack_base.py` along with the
`base issue unresolvable` refusal string this issue named. Re-verified against
`origin/master` at implementation time:

```
'base issue unresolvable' in scripts/fleet/fleet-claim   → 0 occurrences
missing_ancestor_reason  in fleet_stack_base.py          → 1 (a header comment
                                                            recording the retirement)
```

Consequences, each traced to the acceptance criterion it moves:

- **Original AC 1** (`--stackable-on 2508` must fail for a reason other than
  `base issue unresolvable`) is **delivered by #2656 for free** — the code path
  that emits that reason is gone. Implementing Step 2 would be a no-op and
  asserting it would be a vacuous test, so Step 2 is dropped.
- **Original AC 2** ("`test_stack_base_guard.py` gains a case exercising *both*
  surfaces") is unsatisfiable as written — Surface 2 has no code left to
  exercise. The cases land in the two suites that own the surviving surfaces
  instead (`test_enrich_stackable_blocker_prs.py` for the offer,
  `test_fleet_claim_stackable_live_resolve.sh` for the finder); the pure-lib
  walk in `test_stack_base_guard.py` needs no change.
- **Original AC 3** (log the silent non-offer) is unaffected and still the point.
- **Original AC 4** ("existing tests stay green") now means green against the
  post-#2656 tree.

The **original repro pair is also gone** — PR #2508 merged 2026-07-23 and issue
#2513 is closed — so AC 1 was unrunnable on two independent grounds. Per the
plan's own contingency ("if #2508 has merged by then, the stubbed tests are the
acceptance basis — do NOT hold the PR waiting for a live strand"), acceptance is
the stubbed suites. The nearest live specimen, game #349 → PR #348, is a
**partial** repro only: #349 carries two `Blocked by:` refs, so the #1531
multi-blocker limitation declines it regardless of the PR-ref question.

Surviving scope: Steps 1, 3, 4, 5 below. Phase 0's premise probe is dropped with
Step 2 — it existed only to validate the ancestry walk's `gh issue view <PR#>`
root, and there is no longer an ancestry walk.

## Verified current state

- **Surface 1 (offer)** — `fleet-state-scout` `enrich_stackable_blocker_prs`,
  the `matches = [...]` comprehension: arms are
  `branch_matches_issue(headRefName, N)` OR `N in closes_issues`. Both are False
  for a ref naming the PR itself (an issue-less PR has no `claude/<N>-*` branch
  and no `Closes #N`), so `len(matches) != 1` → **silent** `continue`.
- **Surface 3 (finder)** — `cmd_find_stackable_blockers`' open-PR match uses the
  same `branch_matches_issue OR body_closes_issue` union: same gap. Its
  blocker-*resolution* half (`is_satisfied`) needs **no** change:
  `gh issue view <N> --json state` resolves PR numbers, so an open blocker PR
  reads `OPEN` (unresolved, correctly retained) and a merged one reads `MERGED`.
- **The strand is bounded to the pre-merge window.** `check_blockers` and the
  scout's `_resolve_ref_satisfied` both collapse a ref via that same state
  fallback, so once the blocker PR merges the task unblocks normally. Only the
  stacking surfaces were blind.
- **No ambiguity is possible** — GitHub issues and PRs share one number
  namespace, so a ref equal to an open PR's number cannot also be a live issue.

## Approach

**Step 1 — scout offer** (`scripts/fleet/fleet-state-scout`):
1. Add a third arm, textually last, to the match comprehension:
   `or pr.get("number") == issue_num_int`.
2. Log the previously silent non-offer path (#2442's never-silent lesson):
   0 matches and >1 matches each emit one line naming the repo, task, and
   blocker ref (the >1 case additionally names the candidate PRs).

**Step 2 — claim accept.** *Dropped — see Amendment A1.*

**Step 3 — live finder agreement** (`cmd_find_stackable_blockers`): add the same
arm (`or str(pr.get("number", "")) == blocker`) so the finder prints exactly the
base the offer advertises (#1751 agreement).

**Step 4 — tests** (stubbed, no live gh):
1. `test_enrich_stackable_blocker_prs.py` — the offer surface: a number-matched
   base is offered; a non-matching number is not (negative control); filter (b)
   still suppresses a number-matched base; the arm survives the #2442
   body-stripped 304-reuse shape; the game repo is covered. Plus the logging
   pair: 0-match logs, multi-match logs and names candidates, a *successful*
   offer logs nothing (positive control), and a filter-(b) rejection is not
   misattributed to the match path.
2. `test_fleet_claim_stackable_live_resolve.sh` — the finder surface: T9 a
   PR-number ref returns that PR, T10 an unmatched PR-number ref returns empty
   (the arm is not a wildcard), T11 a `fleet:wip` number-matched base returns
   empty (offer/accept agree).
3. Full existing suite green (`bash scripts/fleet/tests/run_all.sh`).

**Step 5 — docs (same PR):** a subsection in
`docs/design/fleet-queue-stacking.md` defining the form and its resolution
semantics, and a corrected bullet in `docs/agents/TASK-FILING.md` (the existing
one says PR numbers are unreliable and to withhold `human:approved` instead —
now stale).

## Affected files

- `scripts/fleet/fleet-state-scout` — number arm + non-offer logging
- `scripts/fleet/fleet-claim` — number arm in `cmd_find_stackable_blockers`
- `scripts/fleet/tests/test_enrich_stackable_blocker_prs.py` — offer cases
- `scripts/fleet/tests/test_fleet_claim_stackable_live_resolve.sh` — finder cases
- `docs/design/fleet-queue-stacking.md`, `docs/agents/TASK-FILING.md`
- **No change:** `scripts/fleet/fleet_stack_base.py` (Surface 2 already retired)

## Acceptance criteria

Positive-fire (feature ON, observable deltas), all stubbed:

- The enrichment suite asserts `stackable_blocker_pr.number == N` is *attached*
  for a number-matched base (count > 0, not merely "no crash"), and the finder
  suite asserts the PR line is *printed*.
- Each new assertion is shown to fail against `origin/master` — a positive
  control run, not an assumption.
- The suppression path logs, and a successful offer does **not** (both asserted,
  so the log can't degrade to firing unconditionally).
- Base safety is unchanged: a `NOT_STACKABLE_BASE_LABELS` base and an
  area-overlapping base are still suppressed on the number arm.
- All existing fleet suites stay green.

## Gotchas

- Keep the number arm LAST in both match unions — minimal diff, existing paths
  stay textually first.
- Multi-blocker tasks stay out of scope (`_single_blocker_issue`'s single-ref
  contract, the v1 Q3 decision).
- The scout enrichment covers both repos already; nothing game-specific needed.
- The number arm must read `pr["number"]`, never `pr["body"]` — bodies are
  stripped on 304 reuse ticks (#2442).
