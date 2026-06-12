# Plan: fleet: scout epic fetch, project_epic_steward projection + slice, skip-set updates (P3)

- **Issue:** #1664
- **Model:** opus
- **Date:** 2026-06-10
- **Epic:** #1661 — see `.fleet/plans/issue-1661.md` for full context
- **Blocked by:** #1662

## Scope

Teach `fleet-state-scout` about epics and add the steward's projection +
slice. Strictly edge-triggered: every projection item disappears as a direct
consequence of the steward's own writes.

## Affected files

- `scripts/fleet/fleet-state-scout`
  - `fetch_epics(repo)` after `fetch_needs_plan` (~line 199):
    `fetch_issues_by_label(repo, "fleet:epic", with_body=True)`; register
    engine + game in `collect_state()` fetch specs (~lines 1601–1617) —
    +2 gh calls/tick inside the existing `FETCH_MAX_WORKERS=2` pool
  - `_parse_epic_checklist(body)` near `_parse_blocked_by`:
    `re.compile(r'^\s*-\s*\[( |x|X)\]\s.*?#(\d+)', re.MULTILINE)` → list of
    `(child_number, checked)`; store as `issue["checklist"]`, pop the body
    before the state write (epic bodies are large)
  - `Part of epic` parsing where bodies already flow: `fetch_task_queue`
    task dict (~line 436) gains `"epic": _parse_issue_field(body, "Part of
    epic") or None`; same in `resolve_human_approved_blockers` before the
    body pop (~line 1022)
  - `project_epic_steward(state)` + `PROJECTORS` (~line 1379) — the generic
    `update_role_trigger` branch in `tick_once()` (~line 1739) needs no
    change. Ops: `normalize` / `rollup` / `adopt` / `design` / `closeout`
    (full pseudocode in the umbrella plan § Scout projection and the
    planning record); items carry only stable fields, sorted
    deterministically
  - `slice_epic_steward(state)` + `SLICERS` (~line 1562): per-item records
    with epic title, checklist + per-child closed state, design PR records
    via `_slice_pr_with_repo`, plan paths
  - `fleet:design-proposed` joins `REVIEW_SKIP_LABELS` (~line 782),
    `_DESIGN_BLOCK_LABELS` (~line 851), `_merger_action_signal` skip set
    (~line 1248)
  - epics join `refresh_all_details` jobs (~line 701) and the
    `gc_issue_caches` keep-set (~line 753)
- `scripts/fleet/tests/test_epic_steward_projection.py` (model:
  `test_merger_projection.py`, loads the scout by path)
- `scripts/fleet/tests/test_parse_epic_checklist.py`

## Approach

Op definitions (managed epic = has body checklist, or plan file exists for
`normalize`):

- `normalize` — plan file exists at `<repo>/.fleet/plans/issue-<N>.md` but
  no body checklist. Legacy epics with neither emit NOTHING (~17 of them —
  no perma-trigger).
- `rollup` — unchecked checklist child that is closed (closed = not in the
  visible-open union of tasks/human_approved/needs_plan/epics, in
  closed_fleet_queued, or rare `_resolve_ref_satisfied` fallback).
- `adopt` — visible open issue (tasks open/in_progress + human_approved)
  whose `epic` field names an umbrella whose checklist lacks it.
- `design` — `fleet:design-blocked` PR branch-matched
  (`branch_matches_issue`) to a checklist child; PLUS `fleet:design-proposed`
  PRs whose umbrella does NOT carry `fleet:steward-proposal` (proposal
  answered → distribute).
- `closeout` — checklist non-empty and every ref closed, umbrella open.

Edge-consumption map: box checked → rollup gone; checklist line added →
adopt/normalize gone; umbrella closed → closeout gone; design-unblock or
design-propose → design gone; steward-proposal removed → distribute
appears (exactly once, until the steward flips the PR labels).

## Acceptance criteria

- Quiescence: same state twice → identical projection hash.
- Edge-consumption: each op disappears after its steward write (fixtures).
- Legacy checklist-less epic without plan file → zero items.
- `fleet:design-proposed` PR absent from sonnet-reviewer + merger
  projections.
- Checklist parser handles `[ ]`/`[x]`/`[X]`, bold-wrapped refs
  (`- [x] **#1527 — mechanism**` → first ref wins), indented sub-bullets,
  and ignores non-checkbox `#N` lines.
- One live scout tick writes well-formed `projections/epic-steward.json`.

## Gotchas

- NOTHING transient in projection items — no label lists, no timestamps
  (the documented `project_merger` self-trigger lesson, scout ~lines
  1205–1247).
- The plan-file existence check is a cheap local `Path.exists()` against the
  repo checkout — staleness only delays normalize bootstrap by one pull;
  acceptable. Do not add git fetches to the scout.
- No per-tick `gh search` calls — membership beyond the checklist is the
  steward's iteration-time job.
- `_DESIGN_BLOCK_LABELS` already contains a `fleet:design-escalated` member
  that is absent from the labels reference (vestigial) — leave it; just add
  `fleet:design-proposed` alongside.
- Trigger files for an unregistered role are harmless; this merges before
  P4 (#1665).

## Verification

- `python3 scripts/fleet/tests/test_epic_steward_projection.py`
- `python3 scripts/fleet/tests/test_parse_epic_checklist.py`
- Run the scout one tick against live state; inspect
  `~/.fleet/state/projections/epic-steward.json` — epic #1661 should emit
  zero items (checklist synced, no closed children yet).
