---
name: fleet-unblock
description: >-
  Unblocks both fleet repos in one human-cued pass and files what comes
  next — runs the fleet-survey tool to partition the queue (claimable here
  / host-pinned / ghost / in-flight / blocked), then closes delivered ghost
  issues, approves defect-shaped and epic-child work, lifts factual
  needs-human parks, repairs lapsed awaiting-infra parks, triages the
  untriaged set, files verified fleet defects and the next objective
  slices, and hands the human only the merges and product calls. Use when
  the user says "unblock the fleet", "fleet unblock", "why is the fleet
  idle", "is anything really blocked", or "what should the fleet work on
  next"; cue-only, never auto-run.
---

# fleet-unblock (Irreden Engine)

**The flow lives in [`docs/agents/skills/fleet-unblock.md`](../../../docs/agents/skills/fleet-unblock.md).**
Read it first, then apply the deltas below.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repos** | `engine` → `jakildev/IrredenEngine`, `game` → `jakildev/irreden` (the tool's defaults) |
| **objectives-path** | [`docs/design/objectives/`](../../../docs/design/objectives/README.md) (engine only; game objectives live in the game repo and are never cited here) |
| **triage staging dir** | `~/.fleet/triage/` — files named `<owner>-<repo>-sweep-<date>.json` |
| **decisions tool** | `fleet-decisions` (`--repo jakildev/irreden` for the game half) |
| **filing lane** | [`docs/agents/TASK-FILING.md`](../../../docs/agents/TASK-FILING.md) § Agent-approved follow-up lane; `--repo jakildev/irreden` for game-side issues |
| **granted classes** | every row of the authority table, granted by the repo owner to the architect pane's cued runs; revoke by editing this row |
| **feedback file** | `~/.fleet/feedback/<role>.md` (`opus-architect.md` from the architect pane) |

## Engine notes

- The tool reads `~/.fleet/state/state.json`; a stale cache means
  `fleet-up` is not running — say so and stop, do not fall back to raw
  `gh` polling (`docs/agents/FLEET-CACHE.md`).
- `gh issue close`, `gh issue edit`, `gh pr edit` and `fleet-triage-sweep
  apply` run without prompts in the architect worktree (`Bash(gh:*)` and
  the fleet tools are allowed in its settings). The worktree sync uses
  `git checkout -B`; `git reset --hard` is denied by user policy.
- Host pins: this host's key comes from `uname` (`mac` / `linux` /
  `windows`); `fleet:needs-gl-host` work is claimable on `linux` and
  `windows` only. A `pinned_away` row is a satellite-host item, not a
  defect.
- Cross-repo isolation: the game half runs with `--repo game`, and no game
  issue content, path or design language lands in an engine issue, PR or
  commit ([`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
  § Cross-repo information isolation).
- The merge queue and the human-parked decisions come from
  `fleet-decisions`; the report links to it rather than repeating it.
