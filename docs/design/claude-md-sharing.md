# CLAUDE.md sharing — design

## Problem

Multiple creations layer on the engine. Cross-cutting rules — naming
conventions, the ECS footgun, the Bash-tool restrictions, the
information-isolation policy — apply to all of them, but the rules
have historically been duplicated across each creation's `CLAUDE.md`
or referenced only by prose ("see project-root `CLAUDE.md` for
naming"). Both modes rot:

- **Duplication** drifts when one copy is updated and the others
  aren't.
- **Soft references** depend on the agent actually reading the
  pointed-to file. New rules in the project-root `CLAUDE.md` reach
  an agent only if the agent thinks to look there.

A creation also needs the freedom to **opt out** of a baseline rule
when it has a legitimate reason to diverge — e.g., a benchmark
creation that intentionally violates the ECS-tick rule to measure
the slow path.

This doc records the decision: what mechanism, what granularity,
what opt-out form, what discovery model.

---

## Decision

**Single canonical baseline file at `docs/agents/CLAUDE-BASELINE.md`.
the project-root `CLAUDE.md` and each creation's `CLAUDE.md` reference it
by name. Per-section opt-out via heading text in the creation's own
`CLAUDE.md`.**

Concretely:

1. The cross-cutting sections (ECS footgun, naming, style, Bash rules,
   what-belongs-in-CLAUDE.md, cross-repo isolation) live verbatim in
   `docs/agents/CLAUDE-BASELINE.md`. the project-root `CLAUDE.md` no
   longer holds them; instead it points at the baseline file at the
   top.
2. Each creation's `CLAUDE.md` has an `## Inherits from engine
   baseline` block near the top declaring inheritance. Without an
   `Opt-outs` subsection, every baseline section applies. With an
   `Opt-outs` subsection, listed headings are skipped.
3. The baseline file's section headings (`## Naming`, `## Style`,
   etc.) are the **stable identifiers**. Renaming a heading is a
   breaking change to the opt-out API and requires a sweep of every
   creation that references the old name.

### Why this shape

The harness only auto-loads the worktree's root `CLAUDE.md`. It does
not traverse upward or follow includes. Whatever mechanism we pick
must therefore be agent-readable text — agents follow pointers in
prose, they do not parse markdown directives.

That ruled out three alternatives:

- **Symlinks** (`creations/<x>/CLAUDE.md → engine/CLAUDE.md`). Works
  on Linux, breaks on Windows-native, fails entirely for creations
  in their own gitignored repo. Even where it works, the creation
  loses its own additions because the symlink replaces the whole
  file.
- **Include directives** (`<!-- include: engine/CLAUDE.md#naming -->`).
  Nothing parses these; agents would still have to follow the
  reference manually, which is no better than a prose pointer.
- **Build-time merge** (a script that assembles a creation's
  `CLAUDE.md` from a template + baseline). Adds tooling overhead and
  introduces a generated-file invariant (must run after every
  baseline change, must be checked in, must be CI-gated). The
  staleness risk is real and the cognitive cost is non-trivial.

A prose pointer with an explicit opt-out list is what agents
already follow well. The cost is one extra file Read per agent
invocation; the benefit is a single source of truth that creations
in any layout (engine-bundled, separate gitignored repo) can point
to with the same syntax.

---

## Granularity

**Per-section.** Whole-file inheritance is wrong: a creation that
opts out of one rule does not need a way to also discard naming and
ECS conventions. The baseline is naturally divided into independent
sections (one per `## ` heading), each documenting one cross-cutting
concern.

The section heading is the opt-out identifier. Section headings in
the baseline are deliberately stable and descriptive (`## Naming`,
`## Style`, `## Bash tool rules`). A creation opting out of "the
naming rules" writes:

```markdown
- **`## Naming`** — <reason>
```

Verbatim. No abbreviation, no paraphrase. If a future renaming pass
in the baseline changes a heading, every creation referencing the
old heading must update.

---

## Opt-out form

A creation declares inheritance with an `## Inherits from engine
baseline` block. Without opt-outs, the block is one line:

```markdown
## Inherits from engine baseline

Applies the rules in `docs/agents/CLAUDE-BASELINE.md`. No opt-outs.
```

With opt-outs, the same block lists each skipped heading on its own
bullet, with a one-sentence reason:

```markdown
## Inherits from engine baseline

Applies the rules in `docs/agents/CLAUDE-BASELINE.md`, except:

- **`## ECS — the single biggest footgun`** — this creation is a
  benchmark that deliberately measures the per-entity-getComponent
  slow path; the violation lives only in `bench_main.cpp` and is
  the intentional behavior under measurement.
- **`## Cross-repo information isolation`** — this creation lives
  in its own public repo and does not participate in the engine
  fleet's public/private split.
```

Each opt-out:

1. Quotes the baseline heading verbatim in `**` and backticks.
2. Gives a one-sentence reason that names the specific deviation
   (file, behavior, or scope).

Reasons matter because reviewers (human or fleet) need to validate
that the opt-out is justified, not just convenient. An opt-out
without a reason should fail review.

---

## Discovery

**By reference, not inline.** Agents read both files: their starting
`CLAUDE.md` (which the harness auto-loads) and the baseline (which
the inheritance block points them at). Inlining the baseline content
into every creation's `CLAUDE.md` would solve the discovery question
but recreates the duplication problem this design exists to fix.

The cost is one extra Read tool call per agent invocation. The
benefit is that every baseline edit reaches every creation
automatically.

the project-root `CLAUDE.md` follows the same pattern: a top-of-file
pointer at the baseline, then the engine-fleet-specific sections
inline. An engine agent reads one extra file relative to the
pre-design status quo; a creation agent reads the same number of
files (their own `CLAUDE.md` plus engine baseline) but gets a sharper
contract.

---

## Audit: which engine docs are baseline vs engine-internal

The audit asked which sections in `engine/**/CLAUDE.md` are
cross-cutting (extract to baseline) vs engine-internal (leave in
place).

### Extracted to baseline

From the project-root `CLAUDE.md`, six sections all marked "(applies
everywhere)" or universal in scope:

- ECS — the single biggest footgun
- Naming
- Style
- What belongs in CLAUDE.md files
- Bash tool rules
- Cross-repo information isolation

### Stays in the project-root `CLAUDE.md` (engine-fleet-specific)

- **Workflow: parallel agents + PRs** — engine fleet workflow.
- **Model split: Opus for core, Sonnet for the fleet** — engine fleet
  budgeting; creations may have different policies.
- **Cross-platform parity (OpenGL ↔ Metal)** — engine render-specific.
- **Verifying render changes** — engine render pipeline.
- **Build** — engine CMake presets and build paths.
- **Running an executable** — engine `fleet-build`/`fleet-run`
  tooling.
- **Issue/PR labeling discipline** — engine fleet labels
  (`fleet:approved`, etc.). The principle generalizes, but the
  concrete label names are fleet-specific.
- **Project layout** — engine directory tree.

### Stays in `engine/<module>/CLAUDE.md` (all engine-internal)

Every per-module `CLAUDE.md` documents that module's API surface
and gotchas. They are read on demand when an agent navigates into
the module's directory; they are not cross-cutting rules. Examples:

- `engine/render/CLAUDE.md` — pipeline ordering, shader stage map,
  backend parity guards.
- `engine/system/CLAUDE.md` — the three valid tick signatures,
  begin/end/relation tick semantics.
- `engine/script/CLAUDE.md` — sol2 binding-trait pattern,
  `lua_component_pack.hpp` convention.
- `engine/prefabs/irreden/common/CLAUDE.md` — `C_*` component
  conventions, modifier-framework design.
- (and so on for asset, audio, command, common, entity, input,
  math, profile, time, video, window, world, plus prefab modules)

A creation occasionally needs to read one of these (when writing a
new system, a new Lua binding, etc.), but none of them apply by
default to every creation. They are not baseline material.

### Special case: `creations/CLAUDE.md` and `creations/demos/CLAUDE.md`

These two files live in the engine repo and document creation-level
conventions (gitignore policy, CMake boilerplate, demo conventions).
They are not baseline themselves — they sit *between* baseline and
the per-creation files. Each gets its own inheritance block at the
top, so an agent navigating into `creations/` or
`creations/demos/<x>/` and reading the directory's `CLAUDE.md` is
told from the first heading where the baseline lives.

---

## Reference implementation

The PR introducing this design ships:

- `docs/agents/CLAUDE-BASELINE.md` — the baseline file itself.
- the project-root `CLAUDE.md` — pointer at the top, six cross-cutting
  sections removed (now in baseline). Engine-fleet-specific sections
  stay inline.
- `creations/CLAUDE.md` — adds an `## Inherits from engine baseline`
  block at the top.
- `creations/demos/CLAUDE.md` — adds the same block, demonstrating
  the mechanism on a checked-in creation directory.

No creation in the engine repo today has a legitimate reason to opt
out of any baseline section, so the inheritance blocks are
no-opt-out. The opt-out form is documented above with a worked
example so future creations have a template.

---

## Future considerations

### When to add a new baseline section

A rule belongs in baseline only if it satisfies all three:

1. The rule applies to every creation, by default.
2. The rule is independent of engine-internal context (e.g., not
   tied to a specific module's API or the engine's specific build
   tooling).
3. Having two creations both forget the rule is worse than the cost
   of an extra file Read per agent invocation.

When in doubt, leave the rule in `engine/CLAUDE.md`. The cost of a
soft cross-reference between engine internals and a creation that
genuinely needs that rule is low; the cost of a baseline section
that 80% of creations would want to opt out of is high.

### CI validation

A future CI check can validate that every opt-out heading actually
exists in the baseline file. A simple grep across all
`creations/**/CLAUDE.md` for `## Inherits from engine baseline`
blocks, parsing each opt-out heading, and asserting the heading
exists in `docs/agents/CLAUDE-BASELINE.md` would catch
typo'd opt-outs and stale references after a baseline rename.

Out of scope for this PR — the population of creations is small
enough today that the lint check would be busy work. Revisit when
five or more creations carry opt-out blocks.

### Cross-repo creations

A creation that lives in its own gitignored sibling repo (e.g.,
`creations/<name>/` with its own git remote) reads the baseline by
the same path the engine sees: `<engine clone root>/docs/agents/
CLAUDE-BASELINE.md`. The current fleet layout always places the
creation's worktree inside the engine clone, so the path resolves.
A creation that lives outside the engine clone entirely would need
to vendor or path-rewrite the reference; flag that as a follow-up
when the case arises.
