# Plan: fleet — atomic planning claim on a needs-plan issue (re-scoped into #1946)

- **Issue:** #1889
- **Model:** opus
- **Status:** **re-scoped + implemented in #1946 (planning-flow redesign PR 3/5, #1932).**
- **Date:** 2026-06-17 (re-scoped 2026-06-23)

## What this was, and what changed

#1889 set out to close the same-tick double-plan race (#1810 produced **three**
duplicate plan PRs → three add/add conflicts) with two pieces:

1. an atomic `fleet-claim` label lock on a `fleet:needs-plan` issue, acquired
   before a planner reads the thread; and
2. a "skip if an open **plan-doc PR** already references the issue" cross-check
   (the cheap early-out for the already-planned-on-a-prior-tick case).

The **planning-flow redesign (#1932)** then collapsed the separate plan-doc PR
into the implementation PR: the canonical plan is now a **`## Plan` issue
comment**, not a committed/merged plan-doc PR. That makes piece (2)'s plan-doc-PR
matcher obsolete — there is no plan-doc PR to match. So #1889 is **re-scoped and
folded into #1946** (#1932 PR 3):

- **Piece 1 (the lock) — kept, implemented in #1946.** `fleet-claim
  planning-claim` / `planning-release` mirror the `steward-claim` class: a
  sole-holder lex-min `fleet:planning-<host>-<agent>` label on the issue
  (`_cmd_pr_label_claim` / `_acquire_label_on` / `_claim_decision`), plus a
  fourth `cleanup --gh` pass that sweeps stale `fleet:planning-*` labels off open
  `fleet:needs-plan` issues after `FLEET_CLAIM_STALE_SECS_PLANNING` (default
  3600s). This is the same-tick race-closer.
- **Piece 2 (the early-out) — replaced.** Instead of the
  `plan_pr_matches_issue` plan-doc-PR cross-check, `planning-claim` now
  early-outs (return 3, "already planned") when a **`## Plan` comment already
  exists** on the issue (`_issue_has_plan_comment`). Same purpose — cheap skip
  for the already-planned case — keyed on the redesign's canonical artifact.

So `plan_pr_matches_issue` is **not** added to `fleet_branch_match.py` (the
original #1889 step 1); the comment check supersedes it.

## Status of the original blocker

`Blocked by: #1890` (the PLANNING-PROTOCOL Step-0 contract prose) — merged. The
contract the lock implements lives in PLANNING-PROTOCOL.md; #1946 names the
concrete `planning-claim` / `planning-release` commands there (role-doc wiring is
#1932 PR 5 / #1948).

## Tests

`scripts/fleet/tests/test_fleet_claim_planning.sh` — acquire, the `## Plan`
dedup early-out (exit 3), cross-host yield + self-remove, release, and the TTL
sweep (stale swept / fresh kept / override respected). 13/13.

## Deferred (noted, not in #1946)

Retiring `_PLAN_DOC_TITLE` (the scope-shipped layer-4 plan-doc-title guard, #1932
PR 3 scope) is **deferred** as orthogonal cleanup: it is one layer of a
multi-layer guard with its own test suite, and removing it has a narrow
regression risk on any lingering plan-doc PR that still references a still-open
issue. The redesign stops *producing* plan-doc PRs, so the guard is harmless to
keep; retire it in a small standalone follow-up rather than bundling a
guard-removal into the claim-lock PR.
