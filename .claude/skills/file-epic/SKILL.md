---
name: file-epic
description: >-
  Take an approved architect plan and file it as the fleet expects: umbrella
  issue labeled fleet:epic, one child fleet:task per phase, per-ticket plan
  files at ~/.fleet/plans/issue-<N>.md, and post-filing stack validation.
---

# file-epic (Irreden Engine)

**The flow lives in [`docs/agents/skills/file-epic.md`](../../../docs/agents/skills/file-epic.md).**
Read it first, then apply the engine deltas below. This wrapper carries
deltas only — see [`docs/design/skill-sharing.md`](../../../docs/design/skill-sharing.md)
for why.

> **Placement (per #1312):** this skill is repo-tracked as a project skill
> in each repo's `.claude/skills/file-epic/`. It loads by cwd, exactly like
> `commit-and-push` / `review-pr`. The shared *flow* is now single-sourced
> in `docs/agents/skills/file-epic.md`; only the deltas below are per-repo,
> so the engine and game wrappers can no longer drift on the flow itself.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` (engine); pass `--repo game` for `jakildev/irreden` |
| **epic label** | `fleet:epic` |
| **task label** | `fleet:task` |
| **architect plans dir** | `~/.claude/plans/<slug>.md` |
| **plans dir** | `~/.fleet/plans/issue-<N>.md` |
| **repo-side plan path** | `<repo>/.fleet/plans/T-<NNN>.md` |
| **validate-stack command** | `fleet-validate-stack <umbrella>` (add `--repo game` for the game repo) |
| **title area vocabulary** | `engine`, `render`, `engine/voxel`, `game`, etc. (the same scope vocabulary `commit-and-push` uses) |

## Engine notes

- The `fleet-validate-stack` helper (shipped #1317) is the **validate-stack
  command**; it auto-discovers children and fails loudly on a malformed
  body. It is the belt that the step-5 hand-filing convention is the
  suspenders for.
- Engine-repo vs game-repo: most epics target `jakildev/IrredenEngine`.
  The cross-repo info-isolation rule (see
  [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
  §"Cross-repo information isolation") means engine child issues must not
  reference the private game repo by name or feature; scrub a game-authored
  plan before filing engine children.
