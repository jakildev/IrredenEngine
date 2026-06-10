# Plan: fleet: fleet-epic-status helper — epic health dashboard with drift rows (P5)

- **Issue:** #1666
- **Model:** sonnet
- **Date:** 2026-06-10
- **Epic:** #1661 — see `.fleet/plans/issue-1661.md` for full context
- **Blocked by:** #1662

## Scope

One command (`fleet-epic-status <N>`) showing an epic's full health:
umbrella state + steward claim, checklist-vs-discovered drift, per-child
queue/PR/plan state, embedded stack validation. `--json` for the steward
role; table for humans.

## Affected files

- `scripts/fleet/fleet-epic-status` (new executable, python; mirror
  `fleet-validate-stack`'s structure: argparse, repo-slug resolution from
  the fleet state config, sibling module import)
- `scripts/fleet/fleet_validate_stack.py` — if child discovery currently
  lives only in the `fleet-validate-stack` executable, factor
  `discover_children` into the importable module (single-source it; do NOT
  re-implement the search + `is_epic_child` filter)
- `scripts/fleet/install.sh` — SRC/DEST pair (~line 123 area) + `ln -sf`
  stanza (~line 311 area), same pattern as `fleet-gate-status`

## Approach

Output sections:
1. Umbrella: state, labels, `fleet:stewarding-*` holder if any.
2. Drift rows: authoritative children (module discovery) diffed against the
   umbrella body checklist → `missing-from-checklist`, `checked-but-open`,
   `unchecked-but-closed`.
3. Per-child: state, model label, `**Blocked by:**`, open PR + design
   labels — read from `~/.fleet/state/state.json` / projections when fresh
   (same staleness threshold the roles use), live `gh` fallback otherwise.
4. Plan-file presence for umbrella + each child at the repo-side plan path.
5. Embedded `validate_stack()` result.

`--repo game` routes slug + state cache key like the other fleet tools.

## Acceptance criteria

- `fleet-epic-status 1661` prints all five sections correctly against the
  live epic; `--json` output is stable and documented in the protocol's
  references.
- Drift detection verified by hand-desyncing a scratch epic's checklist.
- `fleet-help` lists it; `install.sh` links it.

## Gotchas

- Prefer the scout cache over live `gh` — this runs inside steward
  iterations and must not burn API budget.
- Discovery logic stays single-sourced with validate-stack; two diverging
  child-discovery implementations is exactly the drift this epic kills.

## Verification

- `fleet-epic-status 1661` and `fleet-epic-status 1661 --json | python3 -m
  json.tool`.
- Desync test: uncheck a closed child's box on a scratch epic → drift row
  appears.
