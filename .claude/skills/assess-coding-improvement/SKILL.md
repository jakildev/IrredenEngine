---
name: assess-coding-improvement
description: >-
  After fixing PR review feedback, assess whether the fix reveals a
  generalizable improvement to the fleet's development procedures (style
  guide, coding rules, simplify checks, review criteria, worker direction) and,
  if so, file or append to a fleet:coding-improvement ticket. Auto-invoked as
  the last step of a feedback AMEND (see FLEET-FEEDBACK-HANDLING.md Step i).
  Also use when the user asks "should this be a fleet rule?", "assess coding
  improvement", or "file a coding-improvement". It is a reflection pass — it
  never touches the PR's code, labels, or claim.
---

# assess-coding-improvement (Irreden Engine)

**The flow lives in [`docs/agents/skills/assess-coding-improvement.md`](../../../docs/agents/skills/assess-coding-improvement.md).**
Read it first, then apply the engine deltas below. This wrapper carries
deltas only — see [`docs/design/skill-sharing.md`](../../../docs/design/skill-sharing.md)
for why.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` (game PRs: `jakildev/irreden` — add `--repo jakildev/irreden` to every `gh` call, and read game comments with `fleet-pr comments <N> --repo game`) |
| **comments tool** | `fleet-pr comments <N>` |
| **convention surfaces** | searched in Step 3, in this order (see below) |
| **automated-check surface** | the [`simplify`](../simplify/) skill + its `simplify-*` subagents in [`.claude/agents/`](../../agents/) |
| **review checklist** | the engine checklist in [`.claude/skills/review-pr/SKILL.md`](../review-pr/SKILL.md) |

## Engine convention surfaces (Step 3 search order)

1. [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
   — cross-cutting baseline: naming, the ECS footgun, style, ownership/lifetime,
   IRMath. The authoritative home for a *missing* cross-cutting rule (Class A).
2. `.claude/rules/cpp-*.md` — the C++ coding rules:
   [`cpp-ecs.md`](../../rules/cpp-ecs.md) / [`cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md),
   [`cpp-math.md`](../../rules/cpp-math.md), [`cpp-systems.md`](../../rules/cpp-systems.md),
   [`cpp-lua-enums.md`](../../rules/cpp-lua-enums.md).
3. The **nearest module `CLAUDE.md`** (under `engine/`, `engine/prefabs/`,
   `creations/`) — the right home for a rule that's specific to one subsystem
   rather than cross-cutting.
4. The **automated-check surface** above — for a mechanically-detectable rule
   that should be caught pre-commit (the strongest Class-B target).
5. The **review checklist** above (+ [`docs/agents/skills/review-pr.md`](../../../docs/agents/skills/review-pr.md))
   — the backstop enforcement surface.
6. Worker direction: [`.claude/commands/role-*.md`](../../commands/) and
   [`docs/agents/AUTHOR-PIPELINE.md`](../../../docs/agents/AUTHOR-PIPELINE.md).

## Engine notes

- The label `fleet:coding-improvement` is defined in
  [`scripts/fleet/fleet-labels`](../../../scripts/fleet/fleet-labels) and
  [`docs/agents/fleet-labels-reference.md`](../../../docs/agents/fleet-labels-reference.md);
  `fleet-labels` creates it on the repo if missing.
- Several surfaces (`.claude/commands/role-*.md`, `.claude/skills/**/SKILL.md`)
  are **gated self-config** the fleet can't auto-edit — that's exactly why the
  ticket is filed un-queued (no `human:approved`) for the human to triage.
