---
name: file-epic
description: >-
  Files an approved architect plan as the fleet expects: an umbrella issue
  labeled fleet:epic, one fleet:task child per phase, a ## Plan comment on
  the umbrella and on each child, and post-filing stack validation. Use
  when the user says "file the epic", "ship the epic", "open the tickets
  for this plan", or "proceed" after approving a multi-ticket plan.
---

# file-epic (Irreden Engine)

**The flow lives in [`docs/agents/skills/file-epic.md`](../../../docs/agents/skills/file-epic.md).**
Read it first, then apply the deltas below.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` (engine); pass `--repo game` for `jakildev/irreden` |
| **epic label** | `fleet:epic` |
| **task label** | `fleet:task` |
| **architect plans dir** | `~/.claude/plans/<slug>.md` |
| **validate-stack command** | `fleet-validate-stack <umbrella>` (add `--repo game` for the game repo) |
| **title area vocabulary** | `engine`, `render`, `engine/voxel`, `game`, etc. (the same scope vocabulary `commit-and-push` uses) |

## Engine notes

- The engine's `fleet-queue-ingest` plan gate keys on the child's `## Plan`
  comment, so a child is queue-ready as soon as its comment is posted.
- Engine child issues never reference the private game repo by name or
  feature ([`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
  §"Cross-repo information isolation"); scrub a game-authored plan before
  filing engine children.
