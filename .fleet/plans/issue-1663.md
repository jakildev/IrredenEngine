# Plan: fleet: steward claim class, cleanup pass, design-proposed parked/reconcile exclusions (P2)

- **Issue:** #1663
- **Model:** opus
- **Date:** 2026-06-10
- **Epic:** #1661 — see `.fleet/plans/issue-1661.md` for full context
- **Blocked by:** #1662

## Scope

`fleet-claim` support for the steward: umbrella-issue claim class, stale-claim
cleanup, and the `fleet:design-proposed` exclusions in parked-claim and
reconcile machinery.

## Affected files

- `scripts/fleet/fleet-claim`
  - new `cmd_steward_claim` / `cmd_steward_release` next to the existing
    wrappers (~line 1250): `_cmd_pr_label_claim "fleet:stewarding-"
    "steward-claim" "$@"` etc. — `_acquire_label_on` (~line 356) already
    notes PRs and issues share the labels endpoint, and
    `_cmd_pr_label_claim` (~line 1201) is prefix-parameterized
  - subcommand dispatcher + `--help` entries
  - new `cmd_cleanup_gh` pass after the open-PR pass (~line 1472): scan
    `gh issue list --state open --label fleet:epic --json number,labels`,
    collect `fleet:stewarding-*`, reuse the `label_added_epoch` + TTL +
    `gh_release_label`/`write_orphan_sentinel` skeleton with
    `stale_secs="${FLEET_CLAIM_STALE_SECS_STEWARD:-3600}"`
  - reconcile inline-python: `fleet:design-proposed` joins the R2 carve-out
    (~line 2133) and the R7 neither-label test (~line 2176); R4a (~line 2069)
    flags `design-proposed` coexisting with either design sibling
- `scripts/fleet/fleet_branch_match.py` — `PARKED_PR_LABELS` (line 70) gains
  `fleet:design-proposed`
- `scripts/fleet/tests/` — `test_fleet_claim_steward.sh` (clone of
  `test_fleet_claim_acquire.sh` for the new prefix + cleanup TTL); extend
  `test_fleet_claim_reconcile_heal_design_unblock.sh` with a design-proposed
  PR that must NOT be healed

## Approach

- The GH sole-holder label is the cross-host mutex — identical race
  semantics to `fleet:reviewing-` (apply label → re-read full set → sole
  holder wins, lex-min retries, others yield). No FS lock: cap=1 makes
  within-host contention impossible, and the label is what the second host
  sees.
- `--repo game` routing flows through `repo_from_ns` unchanged.
- The scout already spawns `fleet-claim cleanup --gh` on queue-manager
  projection changes — no new invocation plumbing for the cleanup pass.

## Acceptance criteria

- `fleet-claim steward-claim <umbrella> epic-steward` / `steward-release`
  work against an issue number on both repos; losing claimant self-removes.
- Cleanup releases a stale `fleet:stewarding-*` after TTL with sentinel.
- Reconcile regression: `fleet:wip` + `fleet:design-proposed` PR is NOT
  auto-healed to `fleet:design-unblocked`.
- All three test files green.

## Gotchas

- Do NOT reuse `fleet:claim-*` for umbrellas — that class is retained as
  historical audit, swept against `claude/<N>-*` PR existence, and gated by
  `check_blockers`/model-tag in `cmd_claim`; all three mismatch a transient
  stewardship session.
- The cleanup first pass scans open PRs only — the steward sweep is a
  separate third pass over open `fleet:epic` issues.
- Without the R7 exclusion, reconcile undoes every steward proposal within
  `FLEET_RECONCILE_DRIFT_TICKS` — this is the highest-risk line in the PR;
  the regression test is mandatory.

## Verification

- `bash scripts/fleet/tests/test_fleet_claim_steward.sh`
- `bash scripts/fleet/tests/test_fleet_claim_reconcile_heal_design_unblock.sh`
- Manual: steward-claim a scratch issue, verify lex-min tie-break by racing
  two agents, verify cleanup after exporting a 1-second TTL.
