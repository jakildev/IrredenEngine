---
name: commit-and-push
description: >-
  Stage, commit, push a feature branch, and open a GitHub PR against master for
  the Irreden Engine repo. Use whenever the user says "commit", "commit my
  changes", "commit and push", "open a PR", "make a PR", "wrap up this chunk",
  or otherwise indicates the current slice of work is ready for review. The
  skill assumes the parallel-agent workflow where work happens on short-lived
  feature branches, another agent reviews the PR, and the user merges via the
  GitHub UI. NEVER commit directly to master.
---

# commit-and-push (Irreden Engine)

**The flow lives in [`docs/agents/skills/commit-and-push.md`](../../../docs/agents/skills/commit-and-push.md).**
Read it first, then apply the engine deltas below. This wrapper carries
deltas only — see [`docs/design/skill-sharing.md`](../../../docs/design/skill-sharing.md)
for why. Do **not** invoke proactively — only when the user explicitly asks.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` |
| **default branch** | `master` |
| **remote** | `origin` |
| **branch prefix** | `claude/` |
| **worktree-assert command** | `fleet-assert-worktree` (e.g. `fleet-assert-worktree pool-2`) |
| **claim tool** | `fleet-claim` |
| **simplify skill** | `simplify` (`Skill: simplify`) |
| **scope vocabulary** | `render:`, `engine/voxel:`, `game/nav:`, `build:`, `docs:` — derive from the dominant changed path |
| **visual-file globs** | `engine/render/`, `engine/prefabs/irreden/render/`, any `*.glsl` / `*.metal`, `creations/demos/*/src/**`, `creations/demos/*/main*.cpp` |
| **screenshot skill** | `attach-screenshots` (output under `docs/pr-screenshots/<branch>/`) |
| **info-isolation check** | [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §"Cross-repo information isolation" — scan staged paths with `git diff --cached --name-only -- 'creations/game/'` and the body draft for the game-leakage tokens listed there |
| **co-author trailer** | `Co-Authored-By: Claude <noreply@anthropic.com>` (exact model-versioned form per the harness system prompt) |
| **procedures** | the files in [`procedures/`](procedures/) beside this wrapper |

## Engine procedures

The shared flow's mode-detection and step-8 references resolve to these
engine procedure files (unchanged — they carry the engine's `fleet-claim`,
label, and host specifics). Where a procedure says "`SKILL.md` step N", read
it as **step N of the shared flow** in
[`docs/agents/skills/commit-and-push.md`](../../../docs/agents/skills/commit-and-push.md)
(the step numbers are preserved); this wrapper is the entry point that points
there.

- [`procedures/fleet-stack.md`](procedures/fleet-stack.md) — fleet-claim
  stack chain detection and `--base` chaining.
- [`procedures/cursor-stack.md`](procedures/cursor-stack.md) — cursor-flow
  stacking via `branch.<name>.cursor-stack-base`.
- [`procedures/stackable-on.md`](procedures/stackable-on.md) — single-task
  base resolution + `--stackable-on`.
- [`procedures/pr-body.md`](procedures/pr-body.md) — PR body templates +
  stack-mode deltas.
- [`procedures/host-label.md`](procedures/host-label.md) — the
  `fleet:authored-on-<host>` stamp.
- [`procedures/rebase-guard.md`](procedures/rebase-guard.md) — the
  pre-rebase diff-snapshot guard against silently-dropped hunks.

## Engine notes

- Step 3's screenshot prompt fires only when the diff matches a
  **visual-file glob** and `docs/pr-screenshots/<branch>/` doesn't already
  exist — that directory is the engine's screenshot output path.
- Build/format helpers are `fleet-build` / `fleet-build --target
  format-changed` (see [`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md)).
