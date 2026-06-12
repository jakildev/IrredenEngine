# Plan: fleet: fleet-validate-roles lint for role-sharing contract + validate-stack checklist check (P6)

- **Issue:** #1667
- **Model:** sonnet
- **Date:** 2026-06-10
- **Epic:** #1661 — see `.fleet/plans/issue-1661.md` for full context
- **Blocked by:** #1662

## Scope

Mechanize the role-sharing contract from `docs/design/role-sharing.md`:
every canonical role protocol has a conforming thin wrapper in each
fleet-enabled repo root, every wrapper answers every declared delta key.
Plus `fleet-validate-stack --check-checklist`.

## Affected files

- `scripts/fleet/fleet_validate_roles.py` (new pure module — no `gh` calls,
  file I/O only)
- `scripts/fleet/fleet-validate-roles` (new executable wrapper)
- `scripts/fleet/fleet_validate_stack.py` — `--check-checklist`: warn when
  the umbrella body checklist disagrees with discovered children
- `scripts/fleet/install.sh` — install/link pair
- `scripts/fleet/tests/test_fleet_validate_roles.py` (new)
- `docs/design/role-sharing.md` — document running the lint in the new-role
  checklist (one line; the doc itself is P1's)

## Approach

- Opt-in marker: a `## Repo deltas this flow needs` table inside
  `docs/agents/*-protocol.md`. Protocols without one (REVIEWER-PROTOCOL.md
  etc., different casing, no delta tables) are naturally exempt.
- Parse: protocol table rows → declared key set (bold `**key**` in column
  one); wrapper `## Deltas` table rows → answered set.
- Checks: (a) each marked protocol has, in every repo root from
  `--repo-root` (default: engine root + `creations/game` when present on
  disk), a `.claude/commands/role-*.md` whose body references the protocol
  path; (b) missing key = error, extra key = warn; (c) wrapper pointing at a
  nonexistent protocol = warn.
- Severity split like validate-stack; `--strict` promotes warns.
- Alias map for `role-game-architect.md`'s legacy key names (its
  `game-repo-slug` carries an inverted meaning vs the engine wrapper) — the
  breaking key rename in architect-protocol is deferred; lint passes via
  aliases until then.

## Acceptance criteria

- Exits 0 on the current tree (with aliases).
- Deleting a delta key from the engine epic-steward wrapper → non-zero,
  names the key and the wrapper (negative test in the test file).
- Absent game root → skipped gracefully, never an error.
- `fleet-validate-stack 1661 --check-checklist` passes on the synced
  umbrella; warns after hand-desyncing a scratch epic.

## Gotchas

- Game repo root is gitignored and absent on some hosts — absence is normal,
  not a failure.
- Don't lint addenda sections — wrappers may carry legitimate repo-specific
  responsibilities beyond the delta table (the review-pr checklist
  carve-out precedent).

## Verification

- `python3 scripts/fleet/tests/test_fleet_validate_roles.py`
- `fleet-validate-roles` and `fleet-validate-roles --strict` on the tree.
- `fleet-validate-stack 1661 --check-checklist`.
