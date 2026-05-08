# Migration: role-bound worktree names → generic worktree-N

Background and rationale: [#521](https://github.com/jakildev/IrredenEngine/issues/521).

## What changes

Worker-pool worktrees rename to generic identifiers; singleton
worktrees (architects, reviewers, merger, queue-manager) keep their
role-named directories because their name uniquely identifies their
job and they don't share with a worker pool.

### Engine

| Old name           | New name      | Role today        |
|--------------------|---------------|-------------------|
| `opus-worker-1`    | `worktree-1`  | opus-worker       |
| `opus-worker-2`    | `worktree-2`  | opus-worker       |
| `sonnet-fleet-1`   | `worktree-3`  | sonnet-author     |
| `sonnet-fleet-2`   | `worktree-4`  | sonnet-author     |
| `opus-architect`   | (unchanged)   | opus-architect    |
| `opus-reviewer`    | (unchanged)   | opus-reviewer     |
| `sonnet-reviewer`  | (unchanged)   | sonnet-reviewer   |
| `queue-manager`    | (unchanged)   | queue-manager     |
| `merger`           | (unchanged)   | merger            |

### Game

| Old name (game)    | New name (game)     |
|--------------------|---------------------|
| `opus-worker-1`    | `game-worktree-1`   |
| `opus-worker-2`    | `game-worktree-2`   |
| `game-architect`   | (unchanged)         |

The `game-` prefix on the new game worker names prevents reservation
file collisions: today the reservation primitive is keyed on
worktree basename (no `--repo` namespacing), so engine `worktree-1`
and game `worktree-1` would write to the same JSON file.

## What does NOT change

- The role tag (`@fleet-role` tmux user option) on each pane —
  dispatcher routing still works role-by-role until reservation-aware
  routing lands separately.
- The single-role-per-pane assignment. Today, `worktree-1` always
  runs opus-worker iterations; under issue #521's longer-term
  flexibility model that becomes "any worktree can run any role,"
  but that requires the per-role concurrency cap (separate PR).
- Reservation, claim, heartbeat semantics — they were already
  basename-keyed; the rename just changes the basename.

## How the migration runs

`fleet-up` detects old worktree names on boot. If any old-named
worktree still exists, the boot pauses with one of two outcomes:

- **Old worktree is clean** → fleet-up auto-renames via
  `git worktree move <old> <new>`, renames the matching reservation
  file at `~/.fleet/reservations/<old>.json` →
  `~/.fleet/reservations/<new>.json`, and proceeds.
- **Old worktree is dirty** → fleet-up errors with the dirty-file
  list and exits. The human commits, stashes, or otherwise resolves
  the dirty state, then re-runs `fleet-up`.

The migration is one-shot; subsequent boots find the new names and
skip the migration block entirely.

## Side effects on `~/.fleet/` metadata

- **Reservations** (`~/.fleet/reservations/<worktree>.json`) — renamed
  inline by the migration.
- **Heartbeats** (`~/.fleet/heartbeats/<worktree>`) — left alone.
  Stale entries fall off the witness's window naturally; the next
  heartbeat from the renamed worktree creates a fresh entry.
- **Feedback** (`~/.fleet/feedback/<worktree>.md`) — left alone.
  The file is human-readable per-worktree notes; the human can
  rename or merge by hand if desired.
- **Claims** (`~/.fleet/claims/<task-slug>/owner`) — owners written
  with old names persist. They self-heal: the next worker iteration
  on the renamed worktree calls `fleet-claim claim` (which checks
  the gate against current reservations) and the next
  `commit-and-push` releases the old claim.
- **Closed-counts** (`~/.fleet/closed-counts/<task-id>`) — task-keyed,
  not worktree-keyed; unaffected.

## How to roll back

If something goes wrong post-migration, the rollback is symmetric:

```bash
fleet-down
git worktree move .claude/worktrees/worktree-1 .claude/worktrees/opus-worker-1
git worktree move .claude/worktrees/worktree-2 .claude/worktrees/opus-worker-2
git worktree move .claude/worktrees/worktree-3 .claude/worktrees/sonnet-fleet-1
git worktree move .claude/worktrees/worktree-4 .claude/worktrees/sonnet-fleet-2
mv ~/.fleet/reservations/worktree-1.json ~/.fleet/reservations/opus-worker-1.json
# ... etc.
git checkout master -- scripts/fleet/fleet-up   # or revert the PR
fleet-up
```

## Hard constraints

- **Run with the fleet down.** `fleet-down` first so nothing is
  iterating on a worktree mid-rename.
- **Coordinate with PR #522/#526/#527/#529** if those are still
  open at migration time — those branches were checked out in
  the OLD worktree names. After rename, the branches stay checked
  out in the NEW directories (git worktree move preserves the
  HEAD), so subsequent worker iterations resume there.
- **Architect names are stable.** `opus-architect` and
  `game-architect` are babysit-managed and human-facing; they keep
  their names regardless of the rest of the rename.

## Related

- #521 (parent epic — worktree reservations)
- T-121, T-122, T-123 (the resumption infrastructure that this
  migration enables)
