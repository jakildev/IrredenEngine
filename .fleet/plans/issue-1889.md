# Plan: fleet — atomic claim on a needs-plan issue before planning (dedup duplicate plan PRs)

- **Issue:** #1889
- **Model:** opus
- **Date:** 2026-06-17
- **Blocked by:** #1890 (lands the PLANNING-PROTOCOL Step-0 *contract* prose this tooling implements; #1889 amends that same paragraph to name the concrete command, so they must serialize — editing it in parallel is the exact add/add conflict #1889 prevents)

## Scope

Implement the atomic claim the new PLANNING-PROTOCOL **Step 0** describes: a
`fleet-claim`-style lock on a `fleet:needs-plan` issue, acquired *before* a
planner reads the thread, plus a skip-if-an-open-plan-PR-already-references-the-
issue cross-check. Closes the same-tick race where two opus panes select the
same oldest needs-plan issue, both see no plan PR yet, and both plan it (#1810
produced **three** duplicate plan PRs → three add/add conflicts + review cycles).

The PR cross-check alone does NOT close the same-tick race (both planners pass it
before either opens a PR); the atomic lex-min label claim does. Both are needed:
the cross-check is the cheap early-out for the *already-planned-on-a-prior-tick*
case; the claim is the race-closer for the *same-tick* case.

## Approach (one approach, picked)

The lock fabric already has the exact primitive: `_cmd_pr_label_claim`
(`scripts/fleet/fleet-claim:1194`) — a sole-holder lex-min tie-break via
`_acquire_label_on` / `_claim_decision` (#1384). It already serves four label
classes (`fleet:reviewing-` / `fleet:resolving-` / `fleet:amending-` /
`fleet:stewarding-`). **`steward-claim` is the precise template**: it targets the
*issue* (not a PR), label-only (no FS lock), per-mutex — identical shape to what
planning needs. Mirror it, then bolt the plan-PR cross-check onto the front.

Steps, in order:

1. **`fleet_branch_match.py` — add `plan_pr_matches_issue(head_ref, title, issue, repo)`.**
   Returns True iff a PR is a *plan-doc* PR for `issue`. This is a DELIBERATELY
   different matcher than the existing `branch_matches_issue`: that one is
   tuned NOT to match `docs/plan-<N>-…` branches (so a plan-doc PR doesn't
   strand task pickup — [[feedback_plan_doc_pr_strands_pickup]]). The planning
   cross-check wants the opposite — to recognize plan-doc PRs and skip. Match on
   the union of observed plan-PR shapes (from the #1810 incident:
   `claude/1810-plan`, `claude/1810-plan-doc`, `claude/plan-1810`,
   `docs/plan-1810-deploy-blocker-reconcile`):
     - branch regex (anchored, `<N>` = the issue number, word-boundaried so #18
       doesn't match #1810): `^docs/plan-<N>(-|$)`, `^claude/plan-<N>(-|$)`,
       `^claude/<N>-plan(-doc)?(-|$)`, `^claude/<N>-plan$`.
     - title fallback: case-insensitive `plan .*#?<N>\b` (covers
       "docs: plan #1888 — …" / "Plan: <title> (#N)").
   Add it as a *function on the existing module* — NOT a new module
   ([[project_fleet_py_module_resolution]]: new scout-imported modules are
   fragile; functions on an already-imported module are safe).

2. **`fleet-claim` — `cmd_planning_claim` (wrapper, not a one-liner).** Unlike
   steward-claim it must run the cross-check first:
     a. validate the issue number (reuse the numeric guard pattern).
     b. fetch open PRs exactly as `cmd_claim`'s dedup guard does
        (`fleet-claim:819`): `gh pr list --repo "$repo" --state open
        --json number,headRefName,title` piped to an inline python3 block that
        imports `plan_pr_matches_issue`. If ANY open PR matches → print
        `planning-claim: #<N> already has an open plan PR #<M>; skip` and
        `return 1` (do NOT POST a label).
     c. otherwise delegate to the shared claimer:
        `_cmd_pr_label_claim "fleet:planning-" "planning-claim" "$issue" "$agent" "issue"`.
   Honor the global `--repo game` flag (resolve target repo via `repo_from_ns`,
   same as steward-claim) so game-side needs-plan issues are supported.
3. **`fleet-claim` — `cmd_planning_release`.** One-liner mirroring
   `cmd_steward_release`:
   `_cmd_pr_label_release "fleet:planning-" "planning-release" "${1:-}" "${2:-unknown}" "issue"`.
4. **`fleet-claim` — dispatch + help + doc-comment inventory.** Add
   `planning-claim` / `planning-release` case branches (next to the
   `resolving-claim` / `steward-claim` branches, ~line 3734); add usage lines
   (~line 3975); add to the top-of-file usage comment block (~line 29) and the
   label-class inventory comment (~lines 192-194).
5. **`fleet-claim` — `cmd_cleanup` first-pass TTL sweep (the safety net for a
   planner that crashed before releasing).**
     - `fleet-claim:1435`: add `or name.startswith("fleet:planning-")` to the
       swept-prefix predicate.
     - `fleet-claim:~1457-1479`: the heartbeat-fresh skip currently special-cases
       only `fleet:amending-*`. Extend it to ALSO cover `fleet:planning-*`
       (a heavy plan — read thread + write plan + open docs PR — can exceed the
       30-min `FLEET_CLAIM_STALE_SECS` TTL; the planner touches its heartbeat at
       role step 0, so a fresh heartbeat must keep the claim alive). Parse the
       agent out of `fleet:planning-<host>-<agent>` the same way the amending
       branch does.
     - `fleet-claim:1342-1345`: extend the cleanup help text to mention
       `fleet:planning-*`.
6. **`docs/agents/PLANNING-PROTOCOL.md` — wire the concrete command** (this is
   the paragraph #1890 adds, hence the block dependency):
     - **Step 0**: replace the generic "Acquire a `fleet-claim`-style lock …
       (tracked separately)" with the concrete invocation:
       `fleet-claim planning-claim <N> <your-agent-name>` (engine) /
       `fleet-claim --repo game planning-claim <N> <agent>` (game). State the
       contract: exit 0 → you own the plan, proceed; exit 1 → skip (a plan PR
       already exists OR you lost the same-tick race) — move to the next
       needs-plan issue.
     - **Step 4**: after `gh issue edit … --remove-label "fleet:needs-plan"`,
       add `fleet-claim planning-release <N> <agent>`. Specify that the release
       runs on EVERY exit path of the planning attempt — including the
       "If you disagree with the issue's direction … leave fleet:needs-plan on"
       branch — so the lock is never orphaned by a deliberate no-plan.
7. **Test: `scripts/fleet/tests/test_fleet_claim_planning.sh`** (mirror
   `test_fleet_claim_steward.sh`). Two cases, using the existing test knobs
   (`FLEET_TEST_HOST`, `FLEET_CLAIM_NO_SLEEP=1`, mocked `gh`):
     - **same-tick race**: two agents `planning-claim` the same issue with no
       open plan PR → exactly one exit 0, the other exit 1 (lex-min).
     - **cross-check**: with a mocked open PR on branch
       `docs/plan-<N>-foo`, `planning-claim <N>` exits 1 without POSTing a label.
   Also add a `plan_pr_matches_issue` unit assertion (#18 must NOT match a
   `docs/plan-1810-…` branch — the word-boundary check).

## Affected files

- `scripts/fleet/fleet_branch_match.py` — new `plan_pr_matches_issue()` helper.
- `scripts/fleet/fleet-claim` — `cmd_planning_claim` (+ cross-check),
  `cmd_planning_release`, dispatch/help/doc-comment, cleanup first-pass sweep +
  heartbeat guard extension.
- `docs/agents/PLANNING-PROTOCOL.md` — Step 0 concrete command + Step 4 release
  (amends the paragraph #1890 introduces → **Blocked by #1890**).
- `scripts/fleet/tests/test_fleet_claim_planning.sh` — new race + cross-check test.

## Acceptance criteria

- `fleet-claim planning-claim <N> <agent>` exits 0 and stamps
  `fleet:planning-<host>-<agent>` when no plan PR exists and no competitor holds
  a lower-lex planning label; exits 1 otherwise.
- Two concurrent `planning-claim` calls on one issue → exactly one exit 0
  (validated by the new test under `FLEET_CLAIM_NO_SLEEP=1`).
- With an open `docs/plan-<N>-…` PR present, `planning-claim <N>` exits 1 and
  POSTs no label.
- `planning-release` removes the agent's `fleet:planning-*` label (idempotent).
- `cleanup --gh` sweeps a stale `fleet:planning-*` label off an open issue past
  TTL, but SKIPS it when the agent's heartbeat is fresh.
- `--repo game` routes the label to `jakildev/irreden`.
- PLANNING-PROTOCOL Step 0/Step 4 name the concrete command; both `role-worker`
  and `role-opus-architect` inherit it by reference (no edits to those files).
- All new + existing `scripts/fleet/tests/test_fleet_claim_*.sh` pass.

## Gotchas

- **Do NOT edit `.claude/commands/role-worker.md` or any `SKILL.md`.** Those are
  gated self-config; a queue-sourced worker cannot ship edits to them
  ([[feedback_role_config_self_edit_gated]]). The wiring goes entirely into the
  shared `docs/agents/PLANNING-PROTOCOL.md` (non-gated), which both role files
  already delegate to (role-worker step 2 "follow PLANNING-PROTOCOL.md";
  architect-protocol.md:248). This keeps #1889 worker-shippable.
- **Label-only, no FS lock** — mirror steward-claim, NOT cmd_claim. A
  `$CLAIMS_DIR/$slug` mkdir would wrongly tie planning to the task-claim
  lifecycle (blocker/model gating, in-progress audit). The cross-host label is
  the lock; same-host two-pane races are still resolved because the agent name
  (`worker-1` vs `worker-2`) makes the labels distinct and lex-min picks one.
- **Plan-PR matcher must be the inverse of `branch_matches_issue`.** Reusing
  `branch_matches_issue` for the cross-check would MISS `docs/plan-<N>-…`
  branches (it's deliberately built not to match them). Write the separate
  `plan_pr_matches_issue` and word-boundary the number so #18 ≠ #1810/#1889.
- **The #1890 ordering.** #1890 (approved + human-deferred) adds the Step-0
  paragraph; #1889 amends it. Honor `Blocked by: #1890`. If the human wants the
  tooling before merging #1890, the conflict-free fallback is to land the
  fleet-claim + test + helper now and defer ONLY the PLANNING-PROTOCOL.md edit
  to a follow-up after #1890 merges — but the default recommended path is to
  serialize behind #1890 so Step 0 is named in one place.
- **Heartbeat guard parity.** If you add `fleet:planning-*` to the cleanup sweep
  but forget the heartbeat-fresh skip, a long opus plan iteration will have its
  own claim swept out from under it mid-plan (the #1650-class TTL-lapse bug).
