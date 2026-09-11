---
name: triage-coding-improvements
description: >-
  Drains the fleet:coding-improvement backlog in one human-cued batch —
  sweeps the open tickets, clusters them by target surface, triages each
  with the human (accept / reject / defer / escalate a doc rule into a
  simplify check), applies the accepted changes, and ships one PR per run
  with Closes lines. Use when the user says "triage coding improvements",
  "absorb the coding-improvement backlog", "work through the
  coding-improvement tickets", or "address the coding improvements";
  cue-only, never auto-run.
---

# triage-coding-improvements (Irreden Engine)

**The flow lives in [`docs/agents/skills/triage-coding-improvements.md`](../../../docs/agents/skills/triage-coding-improvements.md).**
Read it first, then apply the deltas below.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` |
| **convention surfaces** | same ordered list as the [`assess-coding-improvement`](../assess-coding-improvement/SKILL.md) wrapper — the two skills are the two ends of one channel |
| **automated-check surface** | the [`simplify`](../simplify/) skill + its `simplify-*` subagents in [`.claude/agents/`](../../agents/) |
| **review checklist** | the engine checklist in [`.claude/skills/review-pr/SKILL.md`](../review-pr/SKILL.md) |
| **commit skill** | [`commit-and-push`](../commit-and-push/SKILL.md) |
| **filing norms** | split-out code work is filed as a **plain issue with no labels** ([`docs/agents/fleet-labels-reference.md`](../../../docs/agents/fleet-labels-reference.md) §"Issue/PR labeling discipline"); the human adds `human:approved` and `fleet-queue-ingest` queues it |
| **scope vocabulary** | `docs/fleet:` for convention-surface batches; `fleet:` when the batch extends `simplify-*` checks or fleet scripts |

## Engine notes

- Validating an ESCALATE into a `simplify-*` subagent (flow Step 4): the
  cited occurrence is usually a merged PR — reconstruct the bad pattern in
  a scratch file under the worktree, confirm the check flags it, delete
  the scratch file before committing.
- Headless sessions edit `.claude/` paths with `fleet-edit`
  ([`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md) §"Editing
  `.claude/` paths in headless mode"); human-cued sessions edit directly.
