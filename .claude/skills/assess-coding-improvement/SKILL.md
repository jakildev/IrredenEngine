---
name: assess-coding-improvement
description: >-
  After PR review feedback is fixed, assesses whether the fix reveals a
  generalizable improvement to the fleet's conventions (style guide,
  coding rules, simplify checks, review criteria, worker direction) and
  files or appends to a fleet:coding-improvement ticket. Auto-invoked as
  the last step of a feedback AMEND (FLEET-FEEDBACK-HANDLING.md Step i);
  also use when the user asks "should this be a fleet rule?", "assess
  coding improvement", or "file a coding-improvement". A reflection pass
  only — it never touches the PR's code, labels, or claim.
---

# assess-coding-improvement (Irreden Engine)

**The flow lives in [`docs/agents/skills/assess-coding-improvement.md`](../../../docs/agents/skills/assess-coding-improvement.md).**
Read it first, then apply the deltas below.

## Deltas (Irreden Engine)

| Delta key | Engine value |
|---|---|
| **repo** | `jakildev/IrredenEngine` (game PRs: `jakildev/irreden` — add `--repo jakildev/irreden` to every `gh` call, and read game comments with `fleet-pr comments <N> --repo game`) |
| **comments tool** | `fleet-pr comments <N>` |
| **convention surfaces** | searched in Step 3, in the order below |
| **automated-check surface** | the [`simplify`](../simplify/) skill + its `simplify-*` subagents in [`.claude/agents/`](../../agents/) |
| **review checklist** | the engine checklist in [`.claude/skills/review-pr/SKILL.md`](../review-pr/SKILL.md) |
| **observations-ledger** | issue [#2903](https://github.com/jakildev/IrredenEngine/issues/2903) (parked `human:owned`; game PRs use the game repo's own ledger delta) |

## Engine convention surfaces (Step 3 search order)

1. [`docs/agents/CLAUDE-BASELINE.md`](../../../docs/agents/CLAUDE-BASELINE.md)
   — the cross-cutting baseline (naming, the ECS footgun, style,
   ownership/lifetime, IRMath); the home for a missing cross-cutting rule
   (Class A).
2. `.claude/rules/cpp-*.md` — [`cpp-ecs.md`](../../rules/cpp-ecs.md),
   [`cpp-ecs-smells.md`](../../rules/cpp-ecs-smells.md),
   [`cpp-math.md`](../../rules/cpp-math.md),
   [`cpp-systems.md`](../../rules/cpp-systems.md),
   [`cpp-lua-enums.md`](../../rules/cpp-lua-enums.md).
3. The nearest module `CLAUDE.md` (under `engine/`, `engine/prefabs/`,
   `creations/`) — for a rule specific to one subsystem.
4. The **automated-check surface** — for a mechanically detectable rule
   (the strongest Class-B target).
5. The **review checklist** (+
   [`docs/agents/skills/review-pr.md`](../../../docs/agents/skills/review-pr.md))
   — the backstop.
6. Worker direction: [`.claude/commands/role-*.md`](../../commands/) and
   [`docs/agents/AUTHOR-PIPELINE.md`](../../../docs/agents/AUTHOR-PIPELINE.md).

`fleet:coding-improvement` is defined in
[`scripts/fleet/fleet-labels`](../../../scripts/fleet/fleet-labels) and
[`docs/agents/fleet-labels-reference.md`](../../../docs/agents/fleet-labels-reference.md);
`fleet-labels` creates it if missing.
