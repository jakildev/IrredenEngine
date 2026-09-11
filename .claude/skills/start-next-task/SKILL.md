---
name: start-next-task
description: >-
  Resets the current worktree to a fresh feature branch for the next chunk
  of work — off the latest origin/master by default, or off the just-opened
  PR's head when a fleet-claim molecule is active or the human cued
  cursor-flow stacking. Use after commit-and-push has opened a PR, or when
  the user says "next task", "start next", "move on", "pull master and
  start fresh", "I merged it", "back to master", "fresh start", "new
  task", or cues stacking with "stack this", "next slice stacked", "keep
  stacking", "stack the next on this PR".
---

# start-next-task (Irreden Engine)

**The flow lives in [`docs/agents/skills/start-next-task.md`](../../../docs/agents/skills/start-next-task.md).**
Read it first, then apply the deltas below.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **default branch** | `master` |
| **remote** | `origin` |
| **branch prefix** | `claude/` |
| **worktree-assert command** | `fleet-assert-worktree` (optionally `fleet-assert-worktree <worktree-basename>`) |
| **fleet doc** | [`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) — see "Stacking in cursor flow" |
| **area examples** | `engine`, `render`, `game`, plus module names (`engine/voxel`, etc.) |

Branch-name examples: standard `claude/engine-velocity-drag-refactor`,
`claude/render-lod-threshold-tuning`; cursor stack
`claude/render-glow-pulse-tuning` (on `claude/render-glow-pulse`); fleet
stack `claude/1234-occupancy-grid`.

## Engine notes

- `fleet-issue view <issue#>` is the cache-aware title lookup for step 5's
  fleet-stack branch name; it falls back to `gh issue view`.
- The macOS Cursor sandbox is the shared flow's "sandbox note" case — run
  `git checkout -B` and the `git config branch.<new>.cursor-stack-base`
  write with the `all` permission so `.git/config` is actually written.
