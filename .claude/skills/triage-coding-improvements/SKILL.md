---
name: triage-coding-improvements
description: >-
  Drain the fleet:coding-improvement backlog in one human-cued batch: sweep
  the open tickets, cluster them by target surface, triage each with the
  human (accept / reject / defer / escalate a doc rule into a simplify
  check), apply the accepted convention changes, and bundle them into one PR
  per run with Closes lines — never one micro-PR per ticket. The consumption
  side of the channel assess-coding-improvement files into. Use when the
  user says "triage coding improvements", "absorb the coding-improvement
  backlog", "work through the coding-improvement tickets", or "address the
  coding improvements". Cue-only — NEVER auto-run: the tickets target gated
  self-config (role docs, skills, review checklists) and stay un-queued
  precisely so a human spends the judgment; this skill is how that judgment
  gets spent.
---

# triage-coding-improvements (Irreden Engine)

**The flow lives in [`docs/agents/skills/triage-coding-improvements.md`](../../../docs/agents/skills/triage-coding-improvements.md).**
Read it first, then apply the engine deltas below. This wrapper carries
deltas only — see [`docs/design/skill-sharing.md`](../../../docs/design/skill-sharing.md)
for why.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` |
| **convention surfaces** | same ordered list as the [`assess-coding-improvement`](../assess-coding-improvement/SKILL.md) wrapper — the two skills are the two ends of one channel |
| **automated-check surface** | the [`simplify`](../simplify/) skill + its `simplify-*` subagents in [`.claude/agents/`](../../agents/) |
| **review checklist** | the engine checklist in [`.claude/skills/review-pr/SKILL.md`](../review-pr/SKILL.md) |
| **commit skill** | [`commit-and-push`](../commit-and-push/SKILL.md) |
| **filing norms** | split-out code work is filed as a **plain issue with no labels** (see [`docs/agents/fleet-labels-reference.md`](../../../docs/agents/fleet-labels-reference.md) §"Issue/PR labeling discipline"); the human adds `human:approved` and `fleet-queue-ingest` queues it |
| **scope vocabulary** | `docs/fleet:` for convention-surface batches; `fleet:` when the batch extends `simplify-*` checks or fleet scripts |

## Engine notes

- Validating an ESCALATE into a `simplify-*` subagent (flow Step 4): the
  cited Occurrence is usually a merged PR — reconstruct the bad pattern in a
  scratch file under the worktree, confirm the check's grep/criteria flag
  it, then delete the scratch file before committing.
- In a headless session, `.claude/` paths are blocked for `Edit`/`Write`;
  use `fleet-edit` (see [`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md)
  §"Editing `.claude/` paths in headless mode"). Normal human-cued sessions
  edit directly.
