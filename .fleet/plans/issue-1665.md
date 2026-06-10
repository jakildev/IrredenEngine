# Plan: fleet: dispatcher + fleet-up registration for epic-steward, FLEET_EPIC_STEWARD gate (P4)

- **Issue:** #1665
- **Model:** sonnet
- **Date:** 2026-06-10
- **Epic:** #1661 — see `.fleet/plans/issue-1661.md` for full context
- **Blocked by:** #1664

## Scope

Wire the role into the runtime: dispatcher maps, fleet-up worktrees + pane +
bootstrap trigger, config sample. Everything gated opt-in behind
`FLEET_EPIC_STEWARD` (mirror the smoke-worker opt-in pattern).

## Affected files

- `scripts/fleet/fleet-dispatcher`
  - `DISPATCHED_ROLES` (~line 285): append `"epic-steward"`
  - env capture block (~lines 86–100): `_env_epic_steward` /
    `_env_effort_epic_steward` pairs + matching `unset`s
  - `ROLE_CONCURRENCY` (~line 105): default 1
  - `ROLE_MODEL` (~line 318): `$HEAVY_MODEL` (fallback chain comes free via
    `build_dispatch_command`'s heavy-model test, ~line 866)
  - `ROLE_EFFORT` (~line 335): default `xhigh`
  - NO `PERIODIC_REARM_INTERVAL_SECONDS` entry (fully edge-triggered)
- `scripts/fleet/fleet-up`
  - worktrees (~line 365): `ensure_worktree "$ENGINE"
    .claude/worktrees/epic-steward fleet/epic-steward`; game twin inside the
    game block (~line 374+), both gated on `FLEET_EPIC_STEWARD`
  - `reset_worktree` entries (~line 641) + game guard (~lines 643–656)
  - `write_worktree_settings` loop (~line 814) + game block (~line 828; game
    worktree points at `$ENGINE` like other dual-worktree roles)
  - ops-window pane after the smoke-worker block (~lines 1382–1389): same
    `if [[ -d …/epic-steward ]]` pattern, `$TRANSIENT_PANE_CMD`, heavy-tier
    pane label, `set_fleet_role`, `pane_stagger`
  - bootstrap-trigger heredoc (~lines 1042–1049): `("epic-steward",
    lambda p: bool(p.get(...)))` keyed on the slice's items key (crash-resume
    seed)
- `scripts/fleet/fleet-up.conf.sample`: `FLEET_EPIC_STEWARD=1` section
  modeled on the smoke-worker block (~lines 198–209) +
  `FLEET_CONCURRENCY_EPIC_STEWARD=1` + `FLEET_EFFORT_EPIC_STEWARD=xhigh`

## Approach

Mechanical registration following the smoke-worker precedent throughout.
`fleet-dispatch-wrap` needs no change: `/role-epic-steward` resolves by name
(~line 64) and heavy model maps to `FLEET_ROLE_MODEL=opus` (~line 39). The
role stays OUT of the rc=0 dirty-worktree retrigger list (~line 90) — it
doesn't run the commit-and-push reservation flow.

## Acceptance criteria

- `FLEET_EPIC_STEWARD=1 fleet-up` creates both worktrees and the pane;
  unset → no change anywhere.
- Seeded trigger dispatches one iteration; pane returns to bash; dispatch
  record clears; `fleet-claim` lock releases.
- `fleet-down` sweeps the pane (role-agnostic — verify only).

## Gotchas

- Check the slice's actual top-level key for the bootstrap predicate against
  what #1664 shipped — don't guess `items`.
- The pane label should carry the heavy-tier tag like the other heavy panes.

## Verification

- `FLEET_EPIC_STEWARD=1 fleet-up` on a quiet fleet; confirm pane + worktrees.
- `touch ~/.fleet/state/triggers/epic-steward`; watch one dispatch cycle in
  `~/.fleet/logs/dispatcher.log`.
- Unset the flag, re-run fleet-up, confirm no steward artifacts appear.
