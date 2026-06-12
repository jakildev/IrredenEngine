---
name: start-next-task
description: >-
  Reset the current worktree to a fresh feature branch for the next chunk of
  work. In the standard case that means branching off the latest
  origin/master; if the worker has an active fleet-claim molecule (fleet
  stack mode) or the human cued cursor-flow stacking, the new branch
  instead bases on the just-opened PR's head ref so the downstream task's
  diff stays isolated. Use after commit-and-push has opened a PR and the
  user (or you) wants to move on to the next task, OR whenever the user
  says "next task", "start next", "move on", "pull master and start
  fresh", "I merged it", "back to master", "fresh start", "new task", or
  cues stacking with "stack this", "next slice stacked", "keep stacking",
  "stack the next on this PR".
---

# start-next-task (Irreden Engine)

**The flow lives in [`docs/agents/skills/start-next-task.md`](../../../docs/agents/skills/start-next-task.md).**
Read it first, then apply the engine deltas below. This wrapper carries
deltas only — see [`docs/design/skill-sharing.md`](../../../docs/design/skill-sharing.md)
for why.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **default branch** | `master` |
| **remote** | `origin` |
| **branch prefix** | `claude/` |
| **worktree-assert command** | `fleet-assert-worktree` (optionally `fleet-assert-worktree <worktree-basename>`) |
| **fleet doc** | [`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) — see "Stacking in cursor flow" |
| **area examples** | `engine`, `render`, `game`, plus module names (`engine/voxel`, etc.) |

Concrete branch-name examples for the engine:

- Standard: `claude/engine-velocity-drag-refactor`,
  `claude/render-lod-threshold-tuning`, `claude/game-save-format`.
- Cursor stack: `claude/render-glow-pulse-tuning` (stacked on
  `claude/render-glow-pulse`).
- Fleet stack: `claude/1234-occupancy-grid` (issue-number prefix).

## Engine notes

- `fleet-issue view <issue#>` is the cache-aware title lookup used in
  step 5's fleet-stack branch-name derivation; it falls back to
  `gh issue view`.
- The macOS Cursor sandbox is the concrete case of the shared flow's
  "sandbox note" — run the `git checkout -B` and the
  `git config branch.<new>.cursor-stack-base` writes with the `all`
  permission so `.git/config` is actually written.
