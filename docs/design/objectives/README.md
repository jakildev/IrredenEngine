# Objectives — the standing "what" above epics

An **objective** is a human-owned statement of direction with a measurable
definition of done, sitting one level above epics in the work hierarchy:

```
objective  →  epics / standalone issues  →  PRs
(the what)    (the decomposition)           (the how, shipped)
```

Epics already prove the pattern this tier generalizes: the human signs off
on an umbrella plan once and its children flow through filing → planning →
workers without per-child judgment. Objectives aim that same trust one
level up — the human states *where the engine is going* once, and the
architect proposes the decomposition against it instead of waiting to be
handed work. Approval authority does not move: every proposal still waits
for `human:approved` (see "What this tier changes" below).

## File format

One objective per file, `docs/design/objectives/<slug>.md`:

```markdown
# Objective: <outcome phrase>

**Status:** active | paused | achieved

## Outcome
One paragraph describing the end state in engine/user terms — what is
true when this objective is achieved, not the work to get there.

## Done means
Measurable, tree-checkable statements. Same positive-fire bar as
PLANNING-PROTOCOL acceptance criteria, at direction scale: each row
names something observable (a demo that runs, a harness that passes, a
capability a creation can call), never "progress on X".
- [ ] <observable statement>

## Non-goals
What this objective deliberately excludes — the boundary that keeps
sweep proposals from scope-creeping.

## Current state
Where the tree stands against "Done means", with citations (files,
demos, merged PRs). Updated by objectives sweeps; every claim verified
against the tree at update time, never carried forward from memory.

## Progress ledger
| Date | Epic / issue | Delta |
|---|---|---|
```

## Lifecycle

- **Human-owned.** Objectives are the human's direction. They are created
  and amended via PR like any design doc — the human merging the PR *is*
  the sign-off. The architect may draft or propose amendments; it never
  merges them.
- **Status transitions are the human's call.** A sweep may report "every
  Done-means row now verifies" and propose the `achieved` flip; the flip
  itself ships in a human-merged PR.
- **Scope: engine only.** This repo is public. Game-side objectives live
  in the game repo's own docs — never referenced here, per
  [`CLAUDE-BASELINE.md`](../../agents/CLAUDE-BASELINE.md)
  §"Cross-repo information isolation".

## Linkage from issues

Issues and epic umbrellas that serve an objective carry an optional
standalone line in the structured body (parsers ignore it; humans and
sweeps read it):

```
**Objective:** <slug>
```

See [`TASK-FILING.md`](../../agents/TASK-FILING.md) § Single issue. The
sweep uses these back-links plus the progress ledger to attribute shipped
work to the objective without re-deriving it each pass.

## The objectives sweep

The consumption side. On the human's cue, the architect diffs each
`active` objective's "Done means" against the actual tree and files
proposal issues for the gaps — mechanics and rules in
[`architect-protocol.md`](../../agents/architect-protocol.md)
§"Objectives sweep".

## What this tier changes — and what it doesn't

It changes where work *originates*: from "the human invents every task"
to "the architect proposes tasks against a signed direction, and the
human approves". It does **not** change who approves — sweep proposals
are filed unlabeled and wait for `human:approved` like any other filed
task, and they are explicitly outside the agent-approved follow-up lane
(that lane is for verified defect-shaped follow-ups, not direction).

A per-objective pre-approved lane — where children filed under a
signed-off objective queue without per-ticket approval, the way epic
children already do under a signed umbrella — is the natural next step
once sweep proposals have earned trust. It is deliberately **not** part
of this mechanism yet; nothing in the queue gates reads objective files.
