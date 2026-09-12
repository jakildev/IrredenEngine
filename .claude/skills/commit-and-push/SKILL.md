---
name: commit-and-push
description: >-
  Stages, commits, and pushes a feature branch, then opens a GitHub PR
  against master for the Irreden Engine repo — never a direct commit to
  master. Use whenever the user says "commit", "commit my changes",
  "commit and push", "open a PR", "make a PR", "wrap up this chunk", or
  otherwise says the current slice of work is ready for review.
---

# commit-and-push (Irreden Engine)

**The flow lives in [`docs/agents/skills/commit-and-push.md`](../../../docs/agents/skills/commit-and-push.md).**
Read it first, then apply the deltas below. Invoke only when asked.

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
| **sha-pin token** | `@COMMIT_SHA@` — the `attach-screenshots` snippet emits it; step 8 substitutes `git rev-parse HEAD` |
| **info-isolation check** | [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md) §"Cross-repo information isolation" — scan staged paths with `git diff --cached --name-only -- 'creations/game/'` and the body draft for the game-leakage tokens listed there |
| **co-author trailer** | `Co-Authored-By: Claude <noreply@anthropic.com>` (exact model-versioned form per the harness system prompt) |
| **procedures** | the files in [`procedures/`](procedures/) beside this wrapper |

## Engine procedures

The shared flow's mode-detection and step-8 references resolve to these
files; a procedure's "`SKILL.md` step N" means step N of the shared flow.

- [`procedures/fleet-stack.md`](procedures/fleet-stack.md) — fleet-claim
  stack chain detection and `--base` chaining.
- [`procedures/cursor-stack.md`](procedures/cursor-stack.md) — cursor-flow
  stacking via `branch.<name>.cursor-stack-base`.
- [`procedures/stackable-on.md`](procedures/stackable-on.md) — single-task
  base resolution + `--stackable-on`.
- [`procedures/native-stack-link.md`](procedures/native-stack-link.md) —
  the post-open `gh stack link` step every stack mode runs.
- [`procedures/pr-body.md`](procedures/pr-body.md) — PR body template +
  stack-mode deltas.
- [`procedures/host-label.md`](procedures/host-label.md) — the
  `fleet:authored-on-<host>` stamp.
- [`procedures/rebase-guard.md`](procedures/rebase-guard.md) — the
  pre-rebase diff-snapshot guard.

Build/format helpers: `fleet-build`, `fleet-build --target format-changed`
([`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md)).
