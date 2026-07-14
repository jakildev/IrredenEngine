# CLAUDE-BASELINE — cross-cutting rules for engine + creations

Canonical source for the rules that apply to every agent working anywhere
under this engine, across every creation. The project-root `CLAUDE.md`
and each creation's `CLAUDE.md` reference this file by name; treat the
sections below as if they were inlined into the file you started from.

A creation may **opt out** of an individual section by listing its heading
as a bullet under the `## Inherits from engine baseline` block in the
creation's own `CLAUDE.md`. Heading text is the stable identifier — don't
abbreviate or paraphrase. See `docs/design/claude-md-sharing.md` for the
mechanism's design.

---

## ECS — the single biggest footgun

**Never** call `getComponent` or `getComponentOptional` on individual entities
inside a system's per-entity tick function. Each call is a hash-map lookup, a
linear scan of the archetype, and another hash-map lookup — at scale this
dominates the frame.

Fix: add the component to the system's template parameters so it iterates the
dense column directly. Alternatives (in order of preference):

1. Include the component in `createSystem<...>` template params.
2. Cache the data in an existing component at creation time.
3. Use `beginTick` / `endTick` for once-per-frame lookups.
4. Use `relationTick` for per-parent-group lookups.

See `engine/system/CLAUDE.md` for the full tick-function-signature story.

---

## Naming

| Context           | Convention                                         |
|-------------------|----------------------------------------------------|
| Private members   | `m_` prefix                                        |
| Public members    | trailing `_`                                       |
| Components        | `C_` prefix                                        |
| Enum values       | `SCREAMING_SNAKE_CASE`                             |
| Compute shaders   | `c_` prefix                                        |
| Vertex shaders    | `v_` prefix                                        |
| Fragment shaders  | `f_` prefix                                        |
| Geometry shaders  | `g_` prefix                                        |
| Header helpers    | nested `detail` namespace (not anonymous, not feature-named) |
| Include guards    | `<NAME>_H` from the file basename (not `_HPP`, not `#pragma once`) |

Include guards follow `<NAME>_H` derived from the file basename; the engine
is 477:3 `_H` over `_HPP` engine-wide and the prefab render tree is 157/157
`#ifndef` (no `#pragma once`). Don't flag or "fix" an existing `_H` guard to
`_HPP` for "consistency with the `.hpp` extension" — the data runs the other
way.

Prefer descriptive names over abbreviations (`viewCenterIso` not `vcIso`).
Use a lowercase `detail` namespace for header-only helpers under the owning
namespace (`IRSystem::detail`, `IRRender::detail`). Don't use anonymous
namespaces in headers; keep them in `.cpp`.

No prototype-phase infixes (`Test`, `New`, `Tmp`) in production (non-test)
function names — rename an experimental copy before it's wired into the real
path, or the API keeps signalling "experimental" when it's the production code
(`Debug` is fine when it names a real debug *feature*, not a leftover prototype).

---

## Style

- Early return over nested logic.
- `unique_ptr` over `shared_ptr`; raw pointer = non-owning.
- `std::string` over C buffers unless a low-level API requires otherwise.
- No per-entity `getComponent` inside system tick functions.
- Don't add abstractions for one-time operations; don't design for hypothetical
  future requirements.
- Don't add error handling, fallbacks, or validation for scenarios that can't
  happen. Trust internal code. Only validate at system boundaries.
- **Prefer `enum class` over strings for typed categorical fields.** Closed-set
  fields (e.g., light type, text alignment, system name) are
  `enum class TypeName : int { SCREAMING_SNAKE_CASE = 0, ... }` per the naming
  table. Strings are for human-readable text, file paths, and external interop —
  not for closed categorical sets that the framework dispatches on.
- **Components hold data; systems do work.** A component method must read or
  write only the component's own fields. Methods that look up *another* entity
  via a stored `EntityId` (`IREntity::getComponent`, `setComponent`,
  `createEntity`, `setParent`, `getEntity`) belong in a system, an entity
  builder (`Prefab<>::create`), or a prefab-scoped `IRPrefab::Foo::` namespace
  (Pattern B in `engine/prefabs/irreden/render/CLAUDE.md`). See
  `engine/prefabs/CLAUDE.md` §"Component method rules" for the full
  categorization and the documented exceptions (GPU resource RAII, `onDestroy`
  IO cleanup).
- **Prefer culling, alloc-free reuse, and GPU-side computation over CPU-side
  dirty-flag bookkeeping.** Dirty flags are imperative state; every missed
  invalidation path produces silent stale output. For per-frame render work,
  prefer, in order:
  1. Cull with the fixed iso camera angle and known lighting axes.
  2. Reuse containers across frames and clear them without releasing capacity.
  3. Generate derived lighting/render data on the GPU from buffers already
     there, avoiding CPU uploads.
  4. Use content-addressable memoization only as a documented stopgap with a
     follow-up issue for the culling or GPU-compute fix.
  5. Use dirty flags only as a last resort, with a focused test proving every
     input mutation invalidates the cached output.
- **All math primitives flow through `IRMath`.** `glm::*` and `std::min`
  / `std::max` / `std::clamp` / `std::abs` / `std::cos` / `std::lround`
  / etc. should not appear outside the `engine/math/` library. Anywhere
  else (engine prefabs, shaders' CPU feeders, render systems, creations,
  lighting demos), call `IRMath::min(...)`, `IRMath::clamp(...)`,
  `IRMath::length(...)`, `IRMath::roundHalfUp(...)`, etc. The math
  library owns the wrappers (and may add new ones — `IRMath::cos`,
  `IRMath::sqrt`, `IRMath::roundHalfUp` were added in PR #368). The
  rationale is two-fold: (a) one place to swap implementations
  (e.g. switch between glm and a faster custom path) without touching
  every caller, and (b) one place to encode CPU↔GPU consistency rules
  (`IRMath::roundHalfUp` mirrors GLSL/Metal `roundHalfUp` so half-integer
  positions classify the same on both sides). If you need a primitive
  that isn't in `IRMath` yet, add a wrapper in `engine/math/` first,
  then call it. The math library may itself wrap `glm::*` / `std::*`
  internally — that is the **only** place those names should appear.
- **"Set above" is code narration, not a WHY.** Comments that point at
  *where* code is — `// set above`, `// see below`, `// defined above`,
  `// called from X` — narrate location instead of explaining intent.
  The location is already visible in the code; any real rationale belongs
  at the referenced site, not cross-referenced from here. Delete them; if
  the comment carried a genuine WHY, move it to the site it points at.
  (The `simplify` skill's Check 4 greps for these mechanically on new diffs.)
- **Comments capture durable *why*, not change history.** A comment states
  what is true of the code *as it stands* — invariants, contracts,
  conventions, and gotchas a reader needs regardless of how the code got
  here. It does not narrate the investigation or the diff. Keep *"this is
  the ONE place the threshold lives, so every consumer derives the same
  predicate and can't disagree"*; cut *"Before #1882 the gate used
  `|residual| > deadband`… now Y"*, *"#1957 verified… was a misdiagnosis"*,
  *"retired (T-323)"*. The test: if the code had always existed in its
  current form, would you still write this sentence? If no, it's history —
  it belongs in the commit message, the PR body, or a design doc under
  `docs/design/` (or `.fleet/plans/T-*.md`), not the source; leave at most a
  one-token backref (`// see #1910`). This holds at paragraph scale — a
  30-line block tracing a bug's forensic history is the same smell as a
  one-line `// now uses the deferred variant`, and is most common in render
  code.
- **These style and comment rules apply to the Python under `scripts/`
  (fleet automation + render / perf / gui harnesses) too, not just C++.** PEP8
  spacing, import-order, unused imports, and bare-`assert`-as-guard are enforced
  mechanically by **ruff** (`ruff check scripts/`, gated in CI — see BUILD.md §
  "Python (scripts)"). The one-line-comment / WHY-not-narration policy has no
  mechanical ruff rule, so it stays **reviewer-enforced**: write comments that
  explain intent, not code narration, in Python the same as in C++.

---

## What belongs in agent-facing docs

This rule covers every doc loaded into an agent's context: `CLAUDE.md`
files (one per module), `SKILL.md` files under `.claude/skills/`, and
role files under `.claude/commands/role-*.md`. They differ in scope but
share one editorial rule:

**Point, don't dump.** State each rule, table, procedure, or template
once in its canonical home; everywhere else, link to it. Two copies
drift; one copy + N pointers does not. Before adding anything, ask:
"Could I replace this with a one-line link to a doc that already owns
it?" The `## Canonical-home map` below names the common ones.

**Do NOT include** (any agent-facing doc):

- File/directory tree listings or layout blocks. Agents can `Glob`.
- Catalogs of type/class/component/system/function names. Agents `Grep`.
- "Key components" / "Key systems" sections that mirror `ls <dir>`.
- Function-signature catalogs (the signature lives in the header).
- Restated rules owned by another doc — ECS footgun, naming, IRMath,
  Bash rules, cross-repo isolation, build commands, fleet workflow.
  See the canonical-home map for who owns what.

**SKILL.md-specific don'ts:**

- `## When to invoke` / `## Why this exists` sections that paraphrase
  the front-matter `description:` — the description already triggered
  the skill, so a second copy in the body is filler.
- `## Anti-patterns` entries that just restate flow-step requirements.
  Keep entries that capture genuinely non-obvious things to avoid.
- Decorative emoji bullets (`❌`, `✅`). Bare list bullets are the
  codebase convention.
- Procedures owned by a sibling skill. Compose by reference
  (`Skill: simplify`) rather than restating.
- Heavy worked examples (PR-body templates, full code scaffolds,
  HEREDOC blocks) — move to `<skill>/procedures/<topic>.md`.

**Role-file-specific don'ts:**

- Restated protocols owned by another fleet doc — `FLEET.md`,
  `FLEET-RUNTIME.md`, `FLEET-FEEDBACK-HANDLING.md`,
  `FLEET-CROSS-HOST-SMOKE.md`, `REVIEWER-PROTOCOL.md`,
  `FLEET-CACHE.md`, `BUILD.md`. Reference by anchor; don't paraphrase.
- The baseline `## Hard rules` list — it lives in this doc §"Hard rules
  for autonomous fleet roles". Role files keep only role-specific
  exceptions.

**DO include:**

- Design decisions and their rationale ("we use X because Y").
- Constraints and invariants not obvious from the code, with the *why*.
- Gotchas and footguns that have bitten before.
- Conceptual relationships spanning multiple directories.
- Pipeline / ordering constraints that affect correctness.
- Code examples that demonstrate a **pattern** — illustrative names
  are fine, but the pattern is the point.
- Trigger conditions, owner-role boundaries, and behavioral contracts
  that no other doc owns.

**Verify cross-refs before merging.** A pointer that doesn't resolve
is worse than a duplicate.

- Every `[text](path)` / `[text](path#anchor)` resolves to an existing
  file / heading. GitHub anchors are lowercase-kebab of the heading
  text — re-check after any heading rename.
- Section refs cited as `§Foo` match an actual `## Foo` heading. Six
  role files spent months with `(see CRITICAL section above)` pointing
  at a `## CRITICAL` header that never existed (PR #917). Same shape
  of bug to avoid.
- Names cited (type, function, label, task ID) still exist. Renamed-
  and-not-swept names are how `engine/input/CLAUDE.md` ended up
  pointing at a `C_Hitbox2D` that never existed (PR #909).

**The test for inclusion:** would this section survive a rename
refactor, or would it go stale? Names are fine when part of a pattern
example or a gotcha where the specific name is the actionable fix;
they are clutter when they're just listing what exists. If it would
go stale, it doesn't belong.

If you can't find a canonical home for what you're about to write,
that's a signal the topic *needs* one — file an issue rather than
starting a third copy.

---

## Canonical-home map

Where each cross-cutting rule lives. When authoring or improving an
agent-facing doc, link to the canonical home rather than restating.

| Topic | Canonical home |
|---|---|
| ECS footgun · Naming · Style · IRMath · Bash rules · Cross-repo isolation · Engine API removal · Encode contracts in code · Deprecation markers · Hard rules for fleet roles · What belongs in agent-facing docs · Citing source in filed artifacts | this doc (`docs/agents/CLAUDE-BASELINE.md`) |
| Build commands · presets · `fleet-build` · `fleet-run` | `docs/agents/BUILD.md` |
| Fleet workflow · labels · cursor cues · model split · cross-platform parity · stacking | `docs/agents/FLEET.md` |
| Cross-host smoke validation (OpenGL ↔ Metal) | `docs/agents/FLEET-CROSS-HOST-SMOKE.md` |
| Feedback-label handling (AMEND / ESCALATE / labels) | `docs/agents/FLEET-FEEDBACK-HANDLING.md` |
| Per-iteration runtime (heartbeat / exit / shutdown) | `docs/agents/FLEET-RUNTIME.md` |
| Reviewer protocols (stack gating · label-swap · claim · nits) | `docs/agents/REVIEWER-PROTOCOL.md` |
| Architect protocol (startup · loop · filing · planning · design-blocked handling) | `docs/agents/architect-protocol.md` |
| Shared fleet state cache | `docs/agents/FLEET-CACHE.md` |
| ECS smell diagnostics (machine-checkable) | `.claude/rules/cpp-ecs-smells.md` |
| Math substitution rules (machine-checkable) | `.claude/rules/cpp-math.md` |
| System-state smells (machine-checkable) | `.claude/rules/cpp-systems.md` |
| Tick-function signatures · INPUT → UPDATE → RENDER ordering | `engine/system/CLAUDE.md` |
| Component-method tier rules | `engine/prefabs/CLAUDE.md` |
| Asset serialization version-bump | `engine/asset/CLAUDE.md` |
| `--auto-screenshot` contract | `engine/video/CLAUDE.md` |
| PR-body templates · host-stamp logic | `.claude/skills/commit-and-push/procedures/` |

---

## Citing source in filed artifacts

When an artifact you're filing or writing references the codebase —
GitHub issue bodies, PR descriptions, design docs,
review comments — **prefer symbol citations over line-number
citations** for anything that will be read after the next refactor.

| Prefer | Avoid |
|--------|-------|
| `voxel_set_format.cpp::makeModeChunk` | `voxel_set_format.cpp:91` |
| `system_shapes_to_trixel.hpp::beginTick` | `system_shapes_to_trixel.hpp:111` |
| `C_ShapeDescriptor::lodMin_` | `component_shape_descriptor.hpp:26` |

Symbols don't drift; line numbers do. An issue filed today against
"see `<file>:<line>`" routinely points at the wrong code by the time
a worker picks it up — somebody added an import block or a helper
function above the cited line. A symbol citation survives every
refactor short of the rename itself, and a rename leaves a loud
`grep` failure rather than a silent wrong-line read.

**When line numbers are still appropriate:**

- Citations inside a PR body referencing the PR's own diff. The diff
  is frozen; the line number doesn't drift.
- Pointing at a specific unnamed line (e.g. a magic number, an
  inline comment, a hunk of inline shader code). Use symbol-plus-
  offset where possible: ``inside `makeModeChunk`, the second case``.
- Citing a stack trace or compiler diagnostic verbatim.

**Mixed form is fine:** `chunk_header.hpp::makeTag (~line 72)` gives
the symbol for stability plus a hint for the reader's first scroll.
The line is decorative; the symbol is the contract.

This rule applies to any artifact that gets filed against the repo
or shipped to a reviewer. It does not apply to ephemeral logs,
debug output, or scratch notes the human will read immediately.

---

## Bash tool rules

**Every Bash invocation must be a single, simple command.** Never use
shell compound operators (`&&`, `||`, `;`, `|`) to chain commands.
Issue each command as its own separate Bash tool call, or use the
Read/Glob/Grep tools instead of Bash when possible.

**Don't put fallible Bash calls in the same parallel batch.** When you
emit several Bash calls in one turn they run in parallel, and the harness
**cancels the entire batch the moment any one call exits non-zero** — the
siblings come back as `Cancelled: parallel tool call … errored`, which
reads like a hang, not an error. So one `which a b c` that doesn't find a
tool, an `ls`/`cat` on a maybe-absent path, or a `gh` lookup that 404s
silently kills every other call you batched with it. Therefore:

- Only parallel-batch Bash calls you're confident exit 0 (e.g.
  `git branch --show-current` plus `pwd`).
- Issue **probing / maybe-failing** commands (`which`, `ls`/`cat` on
  paths that may not exist, `gh` lookups that may 404, a build that may
  break) **one per turn** — read each result before the next.
- Prefer Read/Glob/Grep over Bash for files and directories; they return
  an empty/no-match result instead of a non-zero exit, so they never
  trigger the cascade.

This compounds with the single-command rule above: even a
single-command-compliant but fallible call like `which some-tool`,
when batched alongside unrelated Bash calls, exits non-zero and
cancels its batch siblings.

- **No `cd <path> && git ...`** — use `git -C <path> ...` instead.
  `cd && git` triggers a hardcoded Claude Code security gate
  ("Compound commands with cd and git require approval to prevent
  bare repository attacks") that **cannot be suppressed** by any
  allowlist or setting. It always prompts interactively.
- **No `cat file || echo fallback`** — use the Read tool for files.
- **No `cmd1 | cmd2`** — run `cmd1`, read the output, then run `cmd2`
  if needed.
- **No `cmd > file`** (the `>` overwrite redirect operator, to any
  path) — Claude Code's Bash tool blocks shell `>` redirects
  regardless of destination. The gate is on the redirect operator
  itself, not the path. (The `>>` append form used for audit logs
  is distinct and may be fine in specific documented cases.) Run the command alone;
  the Bash tool returns stdout in its output (and auto-persists
  large outputs to a side file you can Read on the next iteration
  via the `<persisted-output>` link). If you need a file on disk,
  use the **Write** tool with the captured content — that's a
  different mechanism and does honor `additionalDirectories`.
- **No `sed -n 'N,Mp' file`** — use the Read tool with `offset` and
  `limit` parameters instead. `sed` triggers its own security gate.
- **Use `git -C`** for any git operation on a repo other than the
  current working directory.
- **Use `--repo owner/name`** for any `gh` operation on a repo other
  than the current working directory.
- **Use the Grep tool** instead of `grep` via Bash. The built-in Grep
  tool is already allowlisted and doesn't require approval.
- **Use the Glob tool** instead of `find`. Glob supports patterns like
  `**/*.hpp` and is already allowlisted.
- **Use `--jq`** on `gh` commands instead of piping to `python3` or
  `jq`. Example: `gh pr list --json number,title --jq '.[] | "#\(.number) \(.title)"'`
- **No `git show ref:file | sed/head/tail`** — run `git show ref:file`
  alone, or use `git -C <path> log`/`git -C <path> diff` with
  appropriate flags instead of piping.
- **Body files for `--body-file`** — write them to a worktree-local path
  (e.g. `.review-body.md`, `.merger-body.md`) via the **Write** tool,
  not `/tmp/`; run `rm -f <path>` first if a prior session may have
  left the file behind. **Always use `--body-file` (never inline
  `--body "..."`) for any PR/issue comment containing backticks or `$`** —
  an inline `gh ... comment --body "...\`code\`..."` lets the shell execute
  the backticked text and expand `$vars`, corrupting the comment or running
  arbitrary tokens (observed on PR #1336).
- **Verify before you report a push.** Stalled Bash output can flush late,
  out of order, or duplicated — never state that a commit "landed", or cite a
  SHA, from such a view. Confirm with a fresh `git fetch` then
  `git rev-parse origin/<branch>` matching local `HEAD` first; if any tool
  result looks empty or stale, run one trivial probe before the next
  state-changing call or status-reporting comment (false SHAs `ab12cd34` /
  `f3a9b2c1` were posted this way on PR #1336 — the real commit was different).

This rule exists because the user-level allowlist (`~/.claude/settings.json`)
matches on the first token of each Bash command. A compound command like
`cd path && git log` starts with `cd`, not `git`, so it won't match
`Bash(git:*)` and will always prompt. The bare-repo check for `cd && git`
is an additional hardcoded gate on top of that.

---

## Cross-repo information isolation

**The engine repo (`jakildev/IrredenEngine`) is public. The game repo
(`jakildev/irreden`) is private.** Information flows one way only:

- **Game-side artifacts MAY reference engine.** The engine is public,
  so a game PR description, commit message, issue, or review comment
  can freely cite engine PRs/issues/files. Game agents already do
  this when filing engine issues for cross-repo dependencies.
- **Engine-side artifacts MUST NOT reference game.** Anything the
  engine repo publishes — PR titles, PR descriptions, commit
  messages, review comments, GitHub issue bodies filed on the engine
  — is world-readable. Leaking the game's task
  IDs, feature names, design language, file paths, or repo slug
  exposes private game work.

**Concretely, when filing or writing anything that lands in the
engine repo, never include:**

- Game PR or issue URLs (`jakildev/irreden#41`, etc.).
- The game repo slug `jakildev/irreden`.
- File paths under `creations/game/` (or any other gitignored
  creation that lives in its own repo — `creations/<name>/`).
- Game-specific design language, feature names, mechanics, or
  systems by their game-side names. Talk in engine-level terms only:
  "the prefab system needs a relation cache" rather than "the ant
  pheromone trails need a relation cache for diffusion".

**Cross-repo dependencies — the right pattern:**

1. The work that lands in the engine PR is described in **pure
   engine terms** — generic capabilities, no game-specific
   motivation. The engine issue is self-contained.
2. The game-side PR's description references the engine issue by
   number or PR URL as a `Blocked by:` dependency.

This is the existing convention for `Blocked by:` (see FLEET.md
§"Resource coordination"). The information-isolation rule
generalizes that direction: engine talks engine, game talks both.

**`commit-and-push` checks for this** before opening an engine PR
— it greps the staged diff and the PR body for game-leakage tokens
and warns. Treat the warning as blocking unless the leakage is
intentional and the user explicitly approves.

A creation in its own private repo may participate in this rule by
treating its own repo as the private side and the engine repo as
the public side. A creation that does not participate in the engine
fleet at all may opt out of this section.

---

## Engine API removal rule

**Never remove engine-defined systems, components, or entities.**
External consumers of the engine may not have their code present in any
`creations/` subdirectory — a local grep finding no consumers does not
mean there are no consumers.

When an engine API appears unused locally, the right action is to write
a demo creation for it. Demos serve as living documentation and prevent
the API from appearing dormant to future agents.

If an engine API is genuinely superseded and removal is being considered,
escalate to the human. Do not unilaterally delete engine-level systems,
components, or entities.

---

## Encode contracts in code, not in comments

When a helper documents a precondition in its docstring — `// must be
exactly 4 bytes`, `// caller guarantees non-null`, `// only call from
the render thread` — enforce the precondition with `IR_ASSERT` /
`static_assert` / `if consteval`, not the comment alone. The docstring
explains *why* the constraint exists; the assert ensures that violating
it is loud rather than silent.

This does **not** conflict with "don't add error handling for scenarios
that can't happen." That rule is about not threading defensive checks
through internal code that already has framework guarantees. This rule
is the narrower point: **if you wrote a comment saying "must be X," the
next line should make "not X" a crash, not a silent wrong answer.**

Canonical failure mode this catches: `makeTag(std::string_view s)`
documented as "must be exactly 4 chars," silently zero-pads when
`s.length() < 4`. Two different callers passing `"VOX"` and `"VOXR"`
(or worse, `"VOX"` and `"VOX\0"` via different conversion paths)
produce identical tag bytes with no warning. The bug surfaces only when
the format loader returns the wrong chunk type at runtime.

Concrete forms:

- **Compile-time constraint on a string-literal or template arg →
  `static_assert`.** Cheapest. Use whenever the caller side is
  consteval-reachable.
- **Runtime constraint on a function parameter → `IR_ASSERT(predicate,
  "diagnostic")` at function entry.** Use when the value is
  user-supplied at runtime. This includes *unwritten* invariants —
  parallel containers that must agree in length get
  `IR_ASSERT(parentIdx.size() == joints.size(), ...)` before iterating
  both, even when no comment ever stated the constraint.
- **Shared helper whose precondition violation is UB (div-by-zero, OOB
  index) → the helper asserts its own precondition.** "Callers guard
  `count > 0`" in a comment is not a guard, and UB is platform-divergent
  (x86 traps integer div-by-zero; Apple Silicon silently returns 0). A
  low-level helper called from many modules *is* a system boundary —
  "only validate at system boundaries" licenses this check rather than
  forbidding it.
- **Hybrid → `if consteval` branch that `static_assert`s, falling
  through to a runtime `IR_ASSERT` at the non-consteval branch.** Use
  for helpers that are sometimes called from `constexpr` contexts and
  sometimes from runtime.

The assertion message should restate the constraint in the same words
the docstring uses, so a hit reads as one consistent contract violation
rather than two separate signals.

---

## Deprecation markers

When an API, file format, helper function, or module surface is being
replaced — and the replacement has shipped or has a concrete landing
plan — mark the deprecation explicitly in three places, no exceptions:

1. **At the declaration site.** Add `// DEPRECATED — use <X> instead.`
   directly above the function / struct / enum-value declaration in
   the header. New consumers reading the header see the marker before
   the docstring.
2. **In the module's `CLAUDE.md`.** Add or extend a `## Deprecated`
   section listing the surface, the replacement, and the date / PR /
   issue that marked it.
3. **In any new design doc** that proposes to extend the affected
   surface: explicitly flag that the surface is on its way out.

**Why this matters.** Concurrent fleet work routinely touches the same
module from different angles. A task filed against the wrong format —
because nothing in the code or docs said "this format is being
replaced" — burns a full PR cycle on scaffolding the replacement
supersedes. The canonical failure mode: F-0.7 (#626) filed a
`.txl.json` sidecar against a format that was being migrated to
`.vxs`; the sidecar shipped, was never adopted by the editor, and got
deleted in the cleanup pass that followed (#705). Both the original
filing and the cleanup PR were avoidable with a deprecation marker on
`.txl`.

**Tasks that propose to *extend* a deprecated surface must escalate to
the human** rather than proceeding. The right move is almost always
to redirect work to the replacement.

This rule complements — does not replace — the **Engine API removal
rule** above. Removal of engine ECS surface (systems, components,
entities) is forbidden without explicit human sign-off; deprecation
markers are the slower-moving "this surface is going away eventually"
signal that applies to any API the human has decided to phase out.

**Transition shims** — code intentionally dead once a known milestone lands
(compatibility aliases, legacy-artifact cleaners, deprecated-knob
fallthrough) — carry a `// Remove once <phase> lands (epic #N)` tombstone at
the declaration so the cleanup pass is self-guided instead of depending on
whoever remembers which epic the shim was tied to.

---

## Hard rules for autonomous fleet roles

These apply to every fleet role. Each role file lists only the
additional role-specific restrictions.

- **Never `git push origin master`. Never `--force` push.** Never call
  `gh pr merge`. The human merges. (The sole auto-merge in the fleet is
  the deterministic tier-0 plan-file lane inside the `fleet-rebase`
  script — see FLEET.md § "Who merges". No LLM role merges anything.)
- **Never run `cmake --preset`** — only `cmake --build` against the
  already-configured tree.
- **Never touch the `.claude/worktrees/` layout.**
- **Never use `sudo`, `brew install/upgrade/uninstall`, `apt`, or
  `xcode-select`** — those are human-initiated.
- **Never leave dirty edits uncommitted at the end of an iteration.**
  If you made any changes to the working tree — manual edits, edits
  that simplify applied, fixes from optimize, anything — you MUST
  follow with `commit-and-push` to land them. The next iteration's
  branch switch will discard them. Don't invoke `simplify` standalone
  — let `commit-and-push` invoke it for you.
- **`.fleet/status/*.md` is scout-maintained bookkeeping.** Read
  when a CLAUDE.md pointer directs you to one; never include them in
  a feature PR's diff. See `.fleet/status/README.md`.
- **Edit/Write paths must stay inside your worktree.** The parent
  clone at `/Users/evinjkill/src/IrredenEngine/` and your worktree at
  `.../.claude/worktrees/<your-basename>/` both contain the same tree
  shape, so an Edit aimed at the parent's absolute path will succeed
  silently — but your build runs against the worktree, so the edit
  appears to "do nothing" while quietly orphaning changes in the
  parent clone (potentially clobbering another agent's work). Prefer
  relative paths from your worktree's cwd. If you must use an
  absolute path, it MUST start with
  `/Users/evinjkill/src/IrredenEngine/.claude/worktrees/<your-basename>/`.
  Re-confirm with `pwd` if unsure.
- **Mutating git flows assert their worktree first.** The same
  parent-clone-vs-worktree confusion bites `git` too: a commit, branch
  checkout, detached PR checkout, or stash run with cwd in the shared
  main clone contaminates the checkout other agents share. The
  `commit-and-push`, `start-next-task`, and `fleet-pr-checkout-detached`
  flows now run `fleet-assert-worktree` (exit 1 if cwd is not a
  `.claude/worktrees/*` tree) as a preflight. If it fires, `cd` into your
  worktree and retry — only a human in a deliberate main-clone session
  should set the `FLEET_ALLOW_MAIN_CLONE=1` override.
- **`refs/stash` is shared across all worktrees — never use positional
  stash refs.** Unlike per-worktree `HEAD`, `refs/stash` lives in the
  common git dir, so every worktree on this clone shares one stash
  stack. A bare `git stash pop` / `git stash drop` (or any `stash@{N}`)
  is a TOCTOU race: a parallel agent in another worktree can push/pop
  between your push and your pop, so your positional ref operates on
  *their* entry — silently applying their changes into your tree or
  consuming your WIP. If a fleet flow must stash, tag the entry with a
  worktree-unique message (include your branch name), then at restore
  resolve it by that message and reapply **by commit SHA** (`git stash
  apply <SHA>` — the SHA is immutable; `drop` by re-resolving its
  current `stash@{N}` index, since `git stash drop` needs the
  `stash@{N}` form). Alternative if untracked files aren't needed
  (or combine with `git add -A` first): skip the shared stack — `git
  stash create` returns a SHA without touching `refs/stash`; or use a
  throwaway temp-branch commit / a dedicated `git worktree`. Note:
  `git stash create` has no `-u` equivalent, so it does not capture
  untracked files — do not substitute it for `git stash push -u`
  without staging untracked files first. See
  [`.claude/skills/attach-screenshots/SKILL.md`](../../.claude/skills/attach-screenshots/SKILL.md)
  for the reference message-keyed, SHA-restored pattern.
- **Single-command Bash only** — see [`## Bash tool rules`](#bash-tool-rules)
  above.
- **Edit/Write blocked for `.claude/commands/` files?** The harness
  permission gate blocks these paths even with `Edit(*)`/`Write(*)`
  in the allowlist. Use python3 for OS-level writes (sanctioned via
  `Bash(python3:*)` in the allowlist):
  `python3 -c "f=open(path).read(); assert old in f, 'string not found'; open(path, 'w').write(f.replace(old, new, 1))"`
