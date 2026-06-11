# Role sharing — design

Sibling to [`skill-sharing.md`](skill-sharing.md) (shared **skill** flows)
and [`claude-md-sharing.md`](claude-md-sharing.md) (shared **rules**). This
doc extends the same pattern to **fleet roles**: the shared *protocol* of a
role lives in one canonical doc, and each repo's
`.claude/commands/role-<name>.md` is a thin wrapper that references it and
supplies only that repo's deltas.

## Problem

Fleet roles — the architect, and now the epic-steward — are run by every
repo that participates in the fleet (the engine, and each creation layered
on it that runs its own fleet). Role definitions were kept as **full forked
copies** in each repo's `.claude/commands/`: the downstream architect role
began life as a copy of the engine's with the slugs and paths swapped.

The copies differ only in **data** — the repo slug, the worktree path, the
claim-tool namespace flags, the feedback file, the responsibility list —
yet the entire startup/loop/escalation protocol was duplicated. Forks
drift: the engine architect gains a step (the stale-cache guard, the
docs-first design-doc flow) and the downstream copy lags. The same failure
modes `skill-sharing.md` calls out for skills (duplication drifts; soft
references depend on the agent actually reading the pointed-to file) apply
here, and the partial fix already shipped — the architect's shared flow was
factored into [`docs/agents/architect-protocol.md`](../agents/architect-protocol.md)
with per-repo wrappers — proved the pattern works for roles. This doc
formalizes that pattern so new roles are **born canonical** instead of
retrofitted.

---

## Decision

**Each shared role's protocol lives verbatim in a single canonical doc at
`docs/agents/<role>-protocol.md`, phrased in repo-neutral terms. Each
repo's `.claude/commands/role-<name>.md` is reduced to a thin wrapper: the
harness frontmatter (which stays), a one-line pointer at the canonical
protocol, and a `## Deltas` table giving this repo's concrete value for
every delta key the protocol names.**

Concretely:

1. The canonical doc carries startup actions, loop discipline, the role's
   flows, escalation rules, modes, and hard rules. Wherever a step needs a
   repo-specific value it names a **delta key** in bold (e.g.
   **repo-slug**, **worktree-path**, **role-banner**), and lists every key
   in a `## Repo deltas this flow needs` table at the top.
2. The wrapper keeps its YAML frontmatter (`name` + `description`)
   unchanged — the harness reads it as the role's identity — plus the
   `$ARGUMENTS` mode line. The body becomes: a pointer at
   `docs/agents/<role>-protocol.md`, a `## Deltas` table answering each
   declared key with this repo's value, and any genuinely repo-specific
   addenda (e.g. the architect wrapper's responsibility list and
   core-area heuristics).
3. The delta-key names are the **stable contract** between a canonical
   protocol and its wrappers. Renaming a key is a breaking change that
   requires sweeping every wrapper — same discipline as renaming a
   baseline heading in `claude-md-sharing.md`.

### Shared vs. delta — where the line falls

The fleet itself (the `fleet-claim` tool, the `fleet:*` label vocabulary,
`fleet-transition` edges, the scout cache, `~/.fleet/` state and plan
paths) is **shared infrastructure** that both the engine and its creations
run. Fleet mechanics live in the canonical protocol concretely; they are
not engine-specific. What is genuinely per-repo, and therefore a delta:

- **Identity** — the repo slug(s), the clone root(s), the dedicated
  worktree path(s).
- **Role identity** — the `fleet-claim` agent id, the startup banner, the
  feedback file.
- **Tool namespacing** — the `fleet-claim` / `fleet-pr` / `fleet-issue`
  per-repo flags (`--repo game` on the engine host's downstream fleet).
- **Domain addenda** — responsibility lists, core-area path sets, module
  `CLAUDE.md` reading lists. These stay in the wrapper as large but
  legitimate deltas (the skill-sharing "review checklist" carve-out,
  applied to roles).

### Baseline delta-key set

Every role protocol declares at least these keys (a protocol may add
role-specific ones, e.g. the steward's **ledger-branch-prefix** or the
architect's **core-area-paths**):

| Delta key | Meaning |
|---|---|
| **repo-slug** | The repo (`owner/name`) for `gh` calls. |
| **repo-root** | Absolute path of the clone. |
| **worktree-path** | The role's dedicated worktree. |
| **role-name** | The role's `fleet-claim` agent id. |
| **role-banner** | The one-line banner printed at startup. |
| **claim-tool-flags** | Per-repo namespace flags for `fleet-claim`. |
| **feedback-file** | The role's feedback file under `~/.fleet/feedback/`. |

### Why runtime indirection, not a commit-time generator

Same decision and same reasons as `skill-sharing.md`: a generator
introduces a generated-file staleness invariant (the drift this design
removes, relocated), adds tooling for a handful of roles, and the
one-extra-`Read`-per-invocation cost is the identical indirection the role
docs already use for `AUTHOR-PIPELINE.md`, `FLEET-RUNTIME.md`, and
`FLEET-FEEDBACK-HANDLING.md`. The agent **`Read`s the canonical protocol at
invoke time**; the wrapper is data.

---

## Wrapper shape (template)

```markdown
---
name: role-<name>
description: <unchanged — the harness identity line, per-repo by nature>
---

You are the **<role>** agent for the <Repo Name> fleet.

**The shared <role> protocol lives in
[`docs/agents/<role>-protocol.md`](<relative path>).**
Read it first — it owns startup, loop discipline, the flows, escalation,
and the hard rules. This wrapper carries only this repo's deltas +
repo-specific addenda. See [`docs/design/role-sharing.md`](<relative path>)
for the mechanism.

Mode (optional argument): $ARGUMENTS

## Deltas (<Repo Name>)

| Delta key | <Repo> value |
|---|---|
| **<key>** | <value> |
| ... | ... |

## <Repo> addenda   (optional — responsibilities, reading lists)
```

The relative paths differ per repo layout, exactly as in
`skill-sharing.md`: a creation whose worktree sits inside the engine clone
reads the engine's `docs/agents/<role>-protocol.md` directly; a fully
separate clone vendors or path-rewrites the reference.

---

## New-role checklist

A PR that adds a canonical role protocol must, **in the same PR**:

1. Add `docs/agents/<role>-protocol.md` with its
   `## Repo deltas this flow needs` table.
2. Add the engine wrapper `.claude/commands/role-<name>.md` answering
   every declared key.
3. **File the downstream-wrapper follow-up issue on the downstream repo
   before merge** (unlabeled, per
   [`docs/agents/TASK-FILING.md`](../agents/TASK-FILING.md)), naming only
   the protocol path and the delta keys the downstream wrapper must
   answer. The downstream fleet authors its own wrapper — engine PRs
   don't write into the downstream repo.

A role with no second fleet (e.g. the engine-only smoke-worker) stays a
full standalone role doc. Factor a role only when more than one fleet runs
it — same threshold as skills.

---

## Discovery

By reference, not inline — identical to `skill-sharing.md`. When the
dispatcher launches a role the agent reads two files: the wrapper (loaded
by the harness as the role body) and the canonical protocol (the wrapper's
first line points at it).

---

## Reference implementations

- **Architect (retrofit):**
  [`docs/agents/architect-protocol.md`](../agents/architect-protocol.md) +
  the engine wrapper `.claude/commands/role-opus-architect.md` + the
  downstream repo's architect wrapper. Factored before this doc existed;
  the pattern this doc generalizes.
- **Epic-steward (born canonical):**
  [`docs/agents/epic-steward-protocol.md`](../agents/epic-steward-protocol.md) +
  `.claude/commands/role-epic-steward.md`, shipped together with this doc
  — the first role to follow the new-role checklist from birth.

---

## Future considerations

### Lint enforcement

`fleet-validate-roles` (tracked as a child of epic #1661) asserts that
every protocol declaring a delta table has a wrapper in each fleet-enabled
repo root, and that every wrapper answers every declared key — the role
analogue of the skill-sharing CI check. Until it lands, the check is
manual (grep the protocol's bold keys against the wrapper's table). It
starts with an alias map for the downstream architect wrapper's legacy key
names; renaming those to the baseline set is deferred until the lint
exists to catch regressions.

### Protocol versioning

If a protocol change ever requires wrappers to migrate in lockstep (a
renamed delta key, a removed mode), land the protocol change and all
known wrappers in one PR, and let the downstream follow-up issue carry
the migration note. A version field in the delta table is overkill at the
current population (two shared roles); revisit if wrappers start missing
breaking changes.
