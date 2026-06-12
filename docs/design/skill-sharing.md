# Skill sharing — design

Sibling to [`claude-md-sharing.md`](claude-md-sharing.md). That doc shares
cross-cutting **rules** via a canonical baseline a creation references by
name. This doc extends the same pattern to **fleet skills**: the shared
*flow* of a skill lives in one canonical doc, and each repo's `SKILL.md` is
a thin wrapper that references it and supplies only that repo's deltas.

## Problem

Several fleet skills — `commit-and-push`, `review-pr`, `start-next-task`,
`file-epic` — are run by every repo that participates in the fleet (the
engine, and each creation layered on it that runs its own fleet). They were
kept as **full forked copies** in each repo's `.claude/skills/<name>/`.

The copies differ only in **data** — the remote/repo slug, the branch
prefix, the commit scope-prefix vocabulary, the visual-file globs, the
host-smoke labels, the domain-specific review checklist — yet the entire
step-by-step flow was duplicated. Forks drift: a skill gains a step in one
repo and the copies lag. This is how a downstream fleet ended up still
running a retired queue flow long after the engine moved on, and how a stale
cross-reference produced a wrong smoke label on a PR.

The same failure modes `claude-md-sharing.md` calls out for rules
(duplication drifts; soft references depend on the agent actually reading
the pointed-to file) apply here.

---

## Decision

**Each shared skill's flow lives verbatim in a single canonical doc at
`docs/agents/skills/<name>.md`, phrased in repo-neutral terms. Each repo's
`.claude/skills/<name>/SKILL.md` is reduced to a thin wrapper: the harness
frontmatter (which stays), a one-line pointer at the canonical flow, and a
`## Deltas` section giving this repo's concrete value for every delta key the
flow names.**

Concretely:

1. The canonical doc carries the ordered steps, preconditions, invariants,
   anti-patterns, and report shape. Wherever a step needs a repo-specific
   value it names a **delta key** in bold (e.g. **default branch**,
   **branch prefix**, **scope vocabulary**, **review checklist**), and lists
   every key in a `## Repo deltas this flow needs` table at the top.
2. The wrapper keeps its YAML frontmatter (`name` + `description`) unchanged
   — the harness reads it as the skill's identity and trigger surface, and
   the trigger phrasing is itself per-repo. The body becomes: a pointer at
   `docs/agents/skills/<name>.md`, and a `## Deltas` table answering each
   delta key with this repo's value, plus any genuinely repo-specific
   addenda (e.g. the engine's review checklist, or its procedure files).
3. The delta-key names are the **stable contract** between a canonical flow
   and its wrappers. Renaming a key is a breaking change that requires
   sweeping every wrapper — same discipline as renaming a baseline heading
   in `claude-md-sharing.md`.

### Shared vs. delta — where the line falls

The fleet itself (the `fleet-claim` tool, the `fleet:*` label vocabulary,
the stack modes, the scout-driven reviewer loop) is **shared infrastructure**
that both the engine and its creations run. So fleet mechanics live in the
canonical flow concretely; they are not engine-specific. What is genuinely
per-repo, and therefore a delta:

- **Identity** — the repo slug / remote, the default branch.
- **Vocabulary** — commit scope prefixes, child-title `<area>` tokens.
- **Path sets** — the visual-file globs that trigger screenshots, the paths
  the cross-host smoke procedure covers.
- **Domain rules** — `review-pr`'s checklist is inherently repo-specific
  (the engine checks ECS/render invariants; a game checks its simulation
  model). It stays in the wrapper as a large but legitimate delta. This is
  the "genuinely creation-specific skills/parts stay full" carve-out.
- **Procedure files** — the per-skill `procedures/*.md` expansions carry the
  repo's concrete command sequences (its `fleet-claim` calls, label strings,
  host table). They remain beside the wrapper and are referenced from it as
  a delta. A creation may point its wrapper at the engine's procedures when
  they are pure fleet-mechanics, or supply its own; either way the *flow*
  doc never embeds one repo's procedure paths.

A skill with no meaningful shared flow (the rendering/Lua/creation-authoring
skills) stays a full standalone `SKILL.md`. Only skills duplicated across
fleets are factored.

### Why runtime indirection, not a commit-time generator

The ticket called for a decision: a wrapper means the agent **`Read`s the
canonical flow at invoke time** (runtime indirection), OR a commit-time sync
**generates** a full `SKILL.md` from shared-body + deltas (zero runtime
indirection). **We choose runtime indirection**, for the same reasons
`claude-md-sharing.md` rejected a build-time merge:

- A generator introduces a generated-file invariant — it must run after
  every canonical-flow edit, the output must be checked in, and a CI gate
  must assert it is not stale. That staleness risk is exactly the drift this
  design exists to remove, merely relocated from "human forgot to sync the
  fork" to "human forgot to regenerate."
- It adds tooling and cognitive overhead for a population of four skills and
  a handful of repos.
- The harness loads `SKILL.md` as the skill body and does **not** auto-inline
  a referenced doc, but agents already follow prose pointers reliably — this
  is the identical indirection the role docs use for the
  `docs/agents/*-PROTOCOL.md` / `AUTHOR-PIPELINE.md` shared protocols, and
  that `claude-md-sharing.md` uses for `CLAUDE-BASELINE.md`. One extra `Read`
  per skill invocation is the whole cost.

The benefit is the same single-source-of-truth: editing
`docs/agents/skills/<name>.md` changes every repo's behavior at once, with
each repo's deviations explicit and version-controlled in its wrapper.

---

## Wrapper shape (template)

```markdown
---
name: <skill-name>
description: >-
  <unchanged — the harness trigger surface, per-repo by nature>
---

# <skill-name> (<Repo Name>)

**The flow lives in [`docs/agents/skills/<name>.md`](<relative path>).**
Read it first, then apply the deltas below. This wrapper carries deltas
only — see [`docs/design/skill-sharing.md`](<relative path>) for why.

## Deltas (<Repo Name>)

| Delta key | <Repo> value |
|---|---|
| **<key>** | <value> |
| ... | ... |

## <Repo> notes / procedures   (optional — repo-specific addenda)
```

The relative paths differ per repo layout. In the engine repo a skill at
`.claude/skills/<name>/SKILL.md` reaches the canonical flow at
`../../../docs/agents/skills/<name>.md`. A creation whose worktree sits
inside the engine clone uses the same `docs/agents/skills/` path (it reads
the engine's canonical flow directly); a creation in a fully separate clone
vendors or path-rewrites the reference, the same follow-up
`claude-md-sharing.md` flags for out-of-clone creations.

---

## Discovery

By reference, not inline — identical to `claude-md-sharing.md`. When a skill
is invoked the agent reads two files: the wrapper (loaded by the harness as
the skill body) and the canonical flow (the wrapper's first line points at
it). Inlining the flow into every wrapper would solve discovery but recreate
the duplication this design removes.

---

## Reference implementation

The PR introducing this design ships, in the engine repo:

- `docs/agents/skills/{commit-and-push,review-pr,start-next-task,file-epic}.md`
  — the four canonical flows.
- The four `.claude/skills/<name>/SKILL.md` reduced to wrappers (frontmatter
  + pointer + `## Deltas`). The engine `procedures/` subdirectories are
  unchanged and referenced from the wrappers as deltas.
- This design doc, cross-linked from `claude-md-sharing.md`.

The role-doc side of the same inheritance model (downstream role docs
referencing the shared `docs/agents/*-PROTOCOL.md` protocols instead of
carrying copies) is tracked downstream; this design is the engine-owned
skills factoring.

---

## Future considerations

### Sharing the `procedures/` too

The per-skill `procedures/*.md` files are today per-repo deltas. Several
(`rebase-guard.md`, the stack-mode mechanics, `re-review.md`) are pure
fleet-mechanics with no engine-specific data and could themselves be
promoted to `docs/agents/skills/procedures/` and referenced the same way.
Out of scope for the first pass — promote a procedure only once a second
repo would otherwise fork it.

### CI validation

A lint check can assert that (a) every wrapper's `## Deltas` table answers
every delta key its canonical flow names, and (b) every canonical flow has at
least one wrapper. A grep over `docs/agents/skills/*.md` for bold
`**delta keys**` cross-referenced against each `SKILL.md`'s Deltas table
would catch an unanswered key after a flow gains a new one. Out of scope
while the set is four skills; revisit when wrappers start missing keys.

### When to factor a new skill

Factor a skill into a canonical flow only when it is **run by more than one
fleet** and the copies **differ only in data**. A skill unique to one repo,
or whose logic (not just its data) legitimately differs per repo, stays a
full standalone `SKILL.md`. When in doubt, leave it standalone — the cost of
a premature canonical flow that every repo wants to override is higher than
one more fork.
