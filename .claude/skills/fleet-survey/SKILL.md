---
name: fleet-survey
description: >-
  Surveys both fleet repos in one human-cued pass — runs the fleet-survey
  tool to partition the queue (claimable here / host-pinned / ghost /
  in-flight / blocked), audits awaiting-infra parks and stranded WIP PRs,
  lists the approval gap and close-out-ready epics, stages triage for the
  human, files the fleet defects it verified, proposes next chunks per
  objective, and ends with one executable checklist. Use when the user
  says "fleet survey", "survey the fleet", "why is the fleet idle", "is
  anything really blocked", or "what should the fleet work on next";
  cue-only, never auto-run.
---

# fleet-survey (Irreden Engine)

**The flow lives in [`docs/agents/skills/fleet-survey.md`](../../../docs/agents/skills/fleet-survey.md).**
Read it first, then apply the deltas below.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repos** | `engine` → `jakildev/IrredenEngine`, `game` → `jakildev/irreden` (the tool's defaults) |
| **objectives-path** | [`docs/design/objectives/`](../../../docs/design/objectives/README.md) (engine only; game objectives live in the game repo and are never cited here) |
| **triage staging dir** | `~/.fleet/triage/` — files named `<owner>-<repo>-sweep-<date>.json` |
| **decisions tool** | `fleet-decisions` (`--repo jakildev/irreden` for the game half) |
| **filing lane** | [`docs/agents/TASK-FILING.md`](../../../docs/agents/TASK-FILING.md) § Agent-approved follow-up lane; `--repo jakildev/irreden` for game-side issues |
| **feedback file** | `~/.fleet/feedback/<role>.md` (`opus-architect.md` from the architect pane) |

## Engine notes

- The tool reads `~/.fleet/state/state.json`; a stale cache means
  `fleet-up` is not running — say so and stop, do not fall back to raw
  `gh` polling (`docs/agents/FLEET-CACHE.md`).
- Host pins: this host's key comes from `uname` (`mac` / `linux` /
  `windows`); `fleet:needs-gl-host` work is claimable on `linux` and
  `windows` only. A `pinned_away` row is a satellite-host item, not a
  defect.
- Cross-repo isolation: a survey that names game issues runs its game
  half with `--repo game` and never pastes game content into an engine
  issue or PR ([`CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
  § Cross-repo information isolation).
- The merge queue and the human-parked decisions come from
  `fleet-decisions`; the survey report links to it rather than repeating it.
