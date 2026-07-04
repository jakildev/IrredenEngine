# Plan: fleet-queue-ingest — scope-shipped false positive on doc-only / deferral PRs (#2196)

- **Issue:** #2196
- **Model:** opus
- **Date:** 2026-07-04

## Scope

`fleet-queue-ingest`'s scope-shipped pre-flight stamps `fleet:scope-shipped` on
an issue when it finds a merged PR that references it. The genuine-ship predicate
lives in `fleet_scope_shipped.py` as a stack of false-positive rejection layers
(1–5). It has no rejection for a PR that references `#N` **only to mark it
deferred** — so #1640 was falsely stamped from PR #1700
(`render: doc the Metal foreign-canvas R32I second-dispatch read-gap invariant
(#1640 deferred)`), a doc-only, design-blocked PR that documents the gap and
explicitly defers the fix. Because the architect's manual label removal re-admits
the issue to ingest and the old predicate re-matches #1700, the removal never
sticks — an un-winnable label fight that keeps #1640 (and its live blocked
consumer #2091) parked forever.

## Approach chosen (approach 2 in the issue: deferral-aware matching)

Of the three candidate directions, add a **layer-6 deferral-marker guard** to the
existing layered predicate in `fleet_scope_shipped.py`. Rationale for picking it
over the alternatives:

- **Approach 1 (respect prior human removal)** is more general but heavier: it
  needs a per-issue label-event timeline query (extra `gh api` call) plus
  human-vs-bot attribution and merge-timestamp comparison — more surface, more
  failure modes, and it lives in the ingest shell/Python rather than the pure
  predicate the module was designed around.
- **Approach 3 (sticky `[scope-not-shipped]` marker)** is a manual opt-out: it
  gives the human a durable lever but does not auto-fix the class — the human
  must add a token beyond the natural label removal.
- **Approach 2 (deferral-aware)** is the root-cause fix and is architecturally
  consistent — the module is *already* a stack of "reject this incidental-match
  class" layers (no-ref, mentioned-not-shipped, range-endpoint, plan-doc title,
  code-span verb). "A `#N` explicitly marked deferred is not shipped" is exactly
  that shape, is a pure predicate change (no extra API call), and auto-prevents
  the whole doc-the-invariant-and-defer class rather than just #1700.

Verified against the real PR #1700: the false positive is **purely the
title-trust** (`fleet_scope_shipped.py:147`) — `#1640` sits in a `render:`-scoped
title (so `_PLAN_DOC_TITLE` does not match it) and the body is only `Refs #1640`
(a bare mention, no closing verb, already rejected by the body path). So the guard
is applied where the bug is: reject a **title** `#N` reference that carries an
adjacent deferral marker.

## Affected files

- `scripts/fleet/fleet_scope_shipped.py` — add `_ref_is_deferred(text, n)` and a
  layer-6 clause on the title-trust branch; extend the module docstring.
- `scripts/fleet/tests/test_scope_shipped_reference.py` — add layer-6 unit cases
  (the #1640/#1700 shape + true-positive regressions) mirroring the existing
  per-layer test blocks.

## Acceptance criteria

- On the #1640/#1700 shape, `pr_references_issue` / `select_shipped_pr` return
  False / None → ingest does not (re-)stamp `fleet:scope-shipped`.
- True-positive behavior preserved: a genuine `#N: <desc>` title, and a
  `Closes #N` body, still ship; a deferral marker on a *different* `#M` in the
  same title does not suppress the shipped `#N`.
- `python3 -m unittest scripts.fleet.tests.test_scope_shipped_reference` passes.

## Unsticking #1640 (post-merge, one step)

The predicate fix prevents *re*-stamping, but #1640 currently still carries the
stale `fleet:scope-shipped` label, and a stamped issue is not in ingest's pending
set (so ingest never sees it to self-heal). After this PR merges, the label must
be removed from #1640 **once** — with the predicate fixed, ingest will no longer
re-stamp it, so the removal finally sticks and #1640 queues on its already-
plan-reviewed investigation-spike plan (unblocking #2091). Doing the removal
*before* merge would just flap (live ingest re-stamps on the old predicate), so
it is deliberately deferred to a post-merge step called out in the PR body.

## Gotchas

- Bound the deferral marker tightly to the specific `#N` (small gap, `\b`
  anchors), mirroring the existing range/verb-gap guards, so `fix #1234 and defer
  #1235` still ships #1234 while suppressing #1235.
- Guard the **title** path only — the body path already requires a closing verb
  (the opposite signal); a body-wide deferral scan risks suppressing a legitimate
  title-trust ship whose body happens to mention some other deferral.
