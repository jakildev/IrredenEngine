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

Prefer descriptive names over abbreviations (`viewCenterIso` not `vcIso`).
Use a lowercase `detail` namespace for header-only helpers under the owning
namespace (`IRSystem::detail`, `IRRender::detail`). Don't use anonymous
namespaces in headers; keep them in `.cpp`.

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

---

## What belongs in CLAUDE.md files

CLAUDE.md files document **concepts, constraints, and gotchas** — things
that aren't obvious from reading the code. They are NOT inventories.

**Do NOT include:**
- File/directory tree listings or layout blocks. Agents can Glob/Grep.
- Catalogs of type, class, or struct names. Agents can Grep for them.
- Variable or field name references (e.g., `m_foo`, `kBar`). Grep.
- Lists of "files in this module" that just mirror `ls`.

**DO include:**
- Design decisions and their rationale ("we use X because Y").
- Constraints and invariants not obvious from the code ("never call
  getComponent inside a tick function — here's why").
- Gotchas and footguns that have bitten before.
- Conceptual relationships that span multiple directories ("the trixel
  pipeline spans prefabs/render, prefabs/update, and shaders/glsl").
- Pipeline or ordering constraints that affect correctness.
- Code examples that demonstrate a **pattern** (e.g., tick-function
  signatures) — use illustrative names, but the pattern is the point.

Names are fine when they're part of a pattern example or a gotcha where
the specific name is the actionable fix. They're clutter when they're
just listing what exists. The test: "would this section survive a
rename refactor, or would it go stale?" If it would go stale, it
doesn't belong.

---

## Bash tool rules

**Every Bash invocation must be a single, simple command.** Never use
shell compound operators (`&&`, `||`, `;`, `|`) to chain commands.
Issue each command as its own separate Bash tool call, or use the
Read/Glob/Grep tools instead of Bash when possible.

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
  so a game PR description, commit message, issue, review comment,
  or `TASKS.md` blocker can freely cite engine PRs/issues/files. Game
  agents already do this when filing engine issues for cross-repo
  dependencies.
- **Engine-side artifacts MUST NOT reference game.** Anything the
  engine repo publishes — PR titles, PR descriptions, commit
  messages, review comments, `TASKS.md` entries, GitHub issue bodies
  filed on the engine — is world-readable. Leaking the game's task
  IDs, feature names, design language, file paths, or repo slug
  exposes private game work.

**Concretely, when filing or writing anything that lands in the
engine repo, never include:**

- Game task IDs (`game T-005`, or unqualified `T-NNN` IDs that came
  from the game queue).
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
   motivation. The engine task in `TASKS.md` is self-contained.
2. The game-side PR's description / TASKS.md entry references the
   engine task by ID or PR URL as a `Blocked by:` dependency.

This is the existing convention for `Blocked by:` (see queue-manager
role, "Cross-repo work" section). The information-isolation rule
generalizes that direction: engine talks engine, game talks both.

**`commit-and-push` checks for this** before opening an engine PR
— it greps the staged diff and the PR body for game-leakage tokens
and warns. Treat the warning as blocking unless the leakage is
intentional and the user explicitly approves.

A creation in its own private repo may participate in this rule by
treating its own repo as the private side and the engine repo as
the public side. A creation that does not participate in the engine
fleet at all may opt out of this section.
