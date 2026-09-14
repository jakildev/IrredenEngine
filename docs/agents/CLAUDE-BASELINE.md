# CLAUDE-BASELINE — cross-cutting rules for engine + creations

Canonical source for the rules that apply to every agent anywhere under this
engine, across every creation. The project-root `CLAUDE.md` and each
creation's `CLAUDE.md` reference this file by name; treat the sections below
as inlined into the file you started from.

A creation may **opt out** of a section by listing its heading verbatim under
the `## Inherits from engine baseline` block in its own `CLAUDE.md`. Heading
text is the stable identifier (`docs/design/claude-md-sharing.md`).

---

## ECS — the single biggest footgun

**Never** call `getComponent` or `getComponentOptional` on individual entities
inside a system's per-entity tick function; at scale it dominates the frame.

Fix ladder, foreign-entity batching, deferred ops: [`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md) §"The ECS footgun".

Tick signatures: `engine/system/CLAUDE.md`. Machine-checkable smells:
[`.claude/rules/cpp-ecs-smells.md`](../../.claude/rules/cpp-ecs-smells.md).

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
| Globals / thread-locals | `g_` / `t_` — [`.claude/rules/cpp-globals.md`](../../.claude/rules/cpp-globals.md) |

Descriptive names over abbreviations (`viewCenterIso`, not `vcIso`).
Header-only helpers go in a lowercase `detail` namespace under the owning
namespace (`IRSystem::detail`); anonymous namespaces stay in `.cpp` files.
`_H` is the guard form for `.hpp` files too — do not "fix" it to `_HPP`. No
prototype-phase infixes (`Test`, `New`, `Tmp`) in production function names;
`Debug` is fine when it names a real debug feature.

---

## Style

- Early return over nested logic.
- `unique_ptr` over `shared_ptr`; raw pointer = non-owning.
- `std::string` over C buffers unless a low-level API requires otherwise.
- No abstractions for one-time operations; no design for hypothetical
  future requirements.
- No error handling, fallbacks, or validation for scenarios that can't
  happen. Trust internal code; validate at system boundaries.
- **`enum class` over strings for closed categorical fields** (light type,
  text alignment, system name): `enum class T : int { SCREAMING_SNAKE_CASE = 0, ... }`.
  Strings are for human-readable text, paths, and external interop.
- **Components hold data; systems do work.** A component method reads or
  writes only its own fields; anything that looks up another entity by a
  stored `EntityId` belongs in a system, a `Prefab<>::create` builder, or a
  prefab-scoped `IRPrefab::Foo::` namespace. Tiers and exceptions:
  `engine/prefabs/CLAUDE.md` §"Component method rules".
- **Prefer culling, alloc-free reuse, and GPU-side computation over CPU-side
  dirty-flag bookkeeping.** For per-frame render work, in order: cull with
  the fixed iso camera angle and known lighting axes; reuse containers
  across frames without releasing capacity; derive render data on the GPU
  from buffers already there; content-addressable memoization only as a
  documented stopgap with a follow-up issue; dirty flags last, with a test
  proving every input mutation invalidates the cached output
  ([`.claude/rules/cpp-ecs.md`](../../.claude/rules/cpp-ecs.md) §"No dirty flags on components").
- **All math primitives flow through `IRMath`.** `glm::*` and `std::min` /
  `std::max` / `std::clamp` / `std::abs` / `std::cos` / `std::lround` / etc.
  appear only inside `engine/math/`; add a missing wrapper there first.
  Substitution table and detector:
  [`.claude/rules/cpp-math.md`](../../.claude/rules/cpp-math.md).
- **No new mutable namespace-scope variables in headers** (`inline` or
  `extern`); every process- or world-scoped mutable object has an owner, a
  lifecycle, and an accessor —
  [`.claude/rules/cpp-globals.md`](../../.claude/rules/cpp-globals.md).
- **Comments explain the code, never its history.** A comment states a
  contract, invariant, or gotcha the code cannot say for itself; never what
  the code does, how it changed, where related code sits, or an issue or PR
  number. Test: if the code had always existed in this form, would you
  still write the sentence? If not, it belongs in the commit message, the
  PR body, or `docs/design/`. The full rule, the keeper test, and the
  executed ratchet: [`.claude/rules/comments.md`](../../.claude/rules/comments.md).
  `simplify` greps new diffs for location narration.
- **The same rules apply to the Python under `scripts/`.** ruff enforces
  PEP8 spacing, import order, unused imports, and bare-`assert` guards
  (`ruff check scripts/`, CI-gated — BUILD.md §"Python (scripts)"); the
  comment rule's issue-reference ratchet covers Python and shell too, and
  intent-not-narration stays reviewer-enforced in every language.

---

## What belongs in agent-facing docs

Applies to every doc loaded into an agent's context: `CLAUDE.md`,
`SKILL.md`, role files, the `docs/agents/` protocols.

**Point, don't dump.** State each rule, table, procedure, or template once
in its canonical home (the `## Canonical-home map` below) and link to it
everywhere else. If nothing owns the topic, file an issue for a home rather
than starting a third copy.

Do **not** include: file trees, layout blocks, "Key components / systems"
sections (agents `Glob`); catalogs of type, component, system, or function
names or signatures (agents `Grep`; the signature lives in the header);
rules owned by another doc (ECS footgun, naming, IRMath, Bash rules,
cross-repo isolation, build commands, fleet workflow, the `## Hard rules`
list — role files keep only role-specific exceptions); incident narration
(state the rule; an issue number survives only as the tracking pointer of a
live deviation the reader must not re-flag); verification or "double-check"
instructions aimed at the model (name the validator in
[`VALIDATION.md`](VALIDATION.md) instead). In a `SKILL.md`, also not:
sections paraphrasing the front-matter `description:`, anti-pattern lists
restating flow steps, decorative emoji bullets, procedures a sibling skill
owns (compose by reference), heavy worked examples (move to
`<skill>/procedures/<topic>.md`). In a role file, also not: protocols a
fleet doc owns — reference by anchor.

**Do** include: design decisions and their rationale; constraints and
invariants not obvious from the code; gotchas that have bitten; conceptual
relationships spanning directories; ordering constraints that affect
correctness; a code example that demonstrates a *pattern*; trigger
conditions, owner-role boundaries, and behavioral contracts no other doc
owns.

Every `[text](path#anchor)` and `§Heading` citation resolves (anchors are
lowercase-kebab of the heading) and every cited name exists; a section ID
implying a series (`M-2`) has its predecessors in the doc or explains the
numbering at the heading. Before stating a rule absolutely ("never X"),
grep the tree for deliberate counter-examples: narrow the rule, or keep it
absolute and record the exceptions in a `## Live deviations` register with
tracking issues. Cite the **shape** of what you measured, not a raw tally
("no `_HPP` guard exists; the only non-`_H` tokens are three vendored
headers"), or name the scope the number was measured over. Inclusion test:
would this section survive a rename refactor? Names are fine inside a
pattern example or an actionable gotcha, clutter when they list what exists.

---

## Canonical-home map

| Topic | Canonical home |
|---|---|
| Naming · Style · Bash rules · Cross-repo isolation · Engine API removal · Encode contracts in code · Deprecation markers · Hard rules for fleet roles · What belongs in agent-facing docs · Citing source in filed artifacts | this doc (`docs/agents/CLAUDE-BASELINE.md`) |
| What proves a change (validators, harnesses, ratchets, CI) | `docs/agents/VALIDATION.md` |
| Build commands · presets · `fleet-build` · `fleet-run` | `docs/agents/BUILD.md` |
| Fleet workflow · cursor cues · model split · cross-platform parity · stacking · fix-forward · resource coordination | `docs/agents/FLEET.md` |
| Label semantics and the state machine | `docs/agents/fleet-labels-reference.md` · `docs/agents/fleet-state-machine.json` |
| Filing issues · body fields · the agent-approved lane · stacks | `docs/agents/TASK-FILING.md` |
| Planning `fleet:needs-plan` issues · the `## Plan` comment | `docs/agents/PLANNING-PROTOCOL.md` |
| Per-iteration runtime (heartbeat / exit / shutdown / feedback) | `docs/agents/FLEET-RUNTIME.md` |
| Feedback-label handling (AMEND / ESCALATE) | `docs/agents/FLEET-FEEDBACK-HANDLING.md` |
| Cross-host smoke validation (OpenGL ↔ Metal) | `docs/agents/FLEET-CROSS-HOST-SMOKE.md` |
| Reviewer protocols (stack gating · label-swap · claim · nits) | `docs/agents/REVIEWER-PROTOCOL.md` |
| Architect · epic-steward · triage protocols | `docs/agents/<role>-protocol.md` |
| Shared fleet state cache | `docs/agents/FLEET-CACHE.md` |
| ECS footgun (getComponent in ticks) · foreign-entity lookups · deferred entity ops · no dirty flags · system-owned invariants · allocations / manager calls in ticks · ECS naming (rule text) | `.claude/rules/cpp-ecs.md` |
| Lua-facing enums and constants (integer tables, never string-name lookups) · audit hooks | `.claude/rules/cpp-lua-enums.md` |
| ECS smell diagnostics (machine-checkable) | `.claude/rules/cpp-ecs-smells.md` |
| Math substitution rules (machine-checkable) | `.claude/rules/cpp-math.md` |
| System-state smells (machine-checkable) | `.claude/rules/cpp-systems.md` |
| Global-state patterns · header-global ban (machine-checkable) | `.claude/rules/cpp-globals.md` |
| Comment policy — explain, never narrate; no issue/PR numbers (ratcheted) | `.claude/rules/comments.md` |
| Running a rules detector tree-wide · the `creations/` sweep trap | `.claude/rules/README.md` |
| Tick-function signatures · INPUT → UPDATE → RENDER ordering | `engine/system/CLAUDE.md` |
| Component-method tier rules | `engine/prefabs/CLAUDE.md` |
| Asset serialization version-bump | `engine/asset/CLAUDE.md` |
| `--auto-screenshot` contract | `engine/video/CLAUDE.md` |
| PR-body templates · host-stamp logic | `.claude/skills/commit-and-push/procedures/` |

---

## Citing source in filed artifacts

In issue bodies, PR descriptions, design docs, and review comments, prefer
symbol citations over line numbers — symbols survive refactors, and a rename
fails loudly in `grep` instead of silently pointing at the wrong line.

| Prefer | Avoid |
|--------|-------|
| `voxel_set_format.cpp::makeModeChunk` | `voxel_set_format.cpp:91` |
| `C_ShapeDescriptor::lodMin_` | `component_shape_descriptor.hpp:26` |

Line numbers are fine for a PR body citing its own frozen diff, for a
specific unnamed line (prefer symbol-plus-offset: ``inside `makeModeChunk`,
the second case``), and for verbatim stack traces or diagnostics. Mixed form
(`makeTag (~line 72)`) is fine: the symbol is the contract, the line a hint.
Ephemeral logs and scratch notes are exempt.

---

## Bash tool rules

The allowlist matches a command's first token and Claude Code adds hardcoded
gates; each form below prompts or misbehaves under unattended operation.

- **One simple command per Bash call.** No `&&`, `||`, `;`, or `|`: run
  `cmd1`, read its output, then run `cmd2`. Prefer Read/Glob/Grep over Bash
  for files and directories — they return empty instead of exiting non-zero.
- **Don't parallel-batch fallible Bash calls.** A non-zero exit cancels every
  sibling in the batch (`Cancelled: parallel tool call … errored`). Batch
  only calls that certainly exit 0; issue probes (`which`, `ls`/`cat` on a
  maybe-absent path, a `gh` lookup that may 404, a build) one per turn.
- **No `cd <path> && git …`** — use `git -C <path> …`; the compound form
  trips an unsuppressable bare-repository gate.
- **No `cmd > file`** on any host — the `>` operator itself is blocked. Read
  stdout from the tool result (large outputs persist to a side file you can
  Read); write files with the **Write** tool.
- **No `sed -n 'N,Mp' file`** — Read with `offset` / `limit`.
- **No `git show ref:file | sed/head/tail`** — run `git show ref:file` alone.
- **No `cat file || echo fallback`** — Read the file.
- **Grep tool** instead of `grep`, **Glob** instead of `find`, `--jq` on
  `gh` instead of piping to `jq` / `python3`
  (`gh pr list --json number,title --jq '.[] | "#\(.number) \(.title)"'`).
- **Never root a tree search at `creations/` or `.claude/`.** Both own an
  ignore-then-negate block in `.gitignore`; ripgrep's walker (which also
  backs the Grep tool) prunes the children and reports a clean pass over
  ~2 of 273 files. Root at the repo top or a child (`creations/demos`), or
  use `fleet-rules-sweep` (resolves files via `git ls-files`, exits 2 when a
  scope covers nothing) — [`.claude/rules/README.md`](../../.claude/rules/README.md).
- **`--repo owner/name`** on any `gh` call against a repo other than cwd's.
- **`--body-file` (never inline `--body "…"`) for any PR/issue text with
  backticks or `$`** — the shell executes the backticks. Write the body to a
  worktree-local path (`.review-body.md`, `.merger-body.md`) with the Write
  tool; `rm -f` it first if a prior session may have left one.
- **Confirm a push before reporting it.** Stalled Bash output can flush late
  or duplicated; cite a SHA only after `git fetch` and
  `git rev-parse origin/<branch>` matches local `HEAD`.
- **Native-Windows host (MSYS2 / Git Bash) has no Bash-tool sandbox**, so an
  allowlist prefix match is the only auto-approval path: no `$VAR` expansion
  in a command (`printenv NAME`, not `echo $NAME`), and `which tool` /
  `type tool` instead of `command -v tool`.

---

## Cross-repo information isolation

**The engine repo (`jakildev/IrredenEngine`) is public; the game repo
(`jakildev/irreden`) is private.** Game-side artifacts may cite engine PRs,
issues, and files; engine-side artifacts — PR titles and bodies, commit
messages, review comments, issue bodies, `## Plan` comments — never
reference the game. Never include on the engine repo:

- Game PR or issue URLs, or the slug `jakildev/irreden`.
- File paths under `creations/game/` or any other gitignored creation.
- Game-specific design language, feature names, mechanics, or system names.
  Talk in engine terms — "the prefab system needs a relation cache", not
  what the game uses it for.

Cross-repo dependencies: the engine issue is self-contained in engine terms;
the game-side PR references it as a `Blocked by:` dependency (FLEET.md
§"Resource coordination"). `commit-and-push` greps the staged diff and PR
body for game-leakage tokens; treat the warning as blocking unless the user
explicitly approves. A creation in its own private repo participates by
treating its repo as the private side; one outside the engine fleet may opt
out.

---

## Engine API removal rule

**Never remove engine-defined systems, components, or entities.** External
consumers may not be present under `creations/`, so a local grep finding no
users proves nothing. An engine API that looks unused gets a demo creation
(living documentation); a genuinely superseded one is escalated to the human
for removal.

---

## Encode contracts in code, not in comments

A documented precondition — `// must be exactly 4 bytes`, `// caller
guarantees non-null`, `// only call from the render thread` — is enforced
with `IR_ASSERT` / `static_assert` / `if consteval`, not by the comment
alone: the comment explains why, the assert makes a violation loud. This is
the narrow complement of "don't validate what can't happen" — if you wrote
"must be X", the next line makes "not X" a crash. The assertion message
restates the constraint in the docstring's words.

- Compile-time constraint on a literal or template argument →
  `static_assert`.
- Runtime constraint on a parameter → `IR_ASSERT(predicate, "diagnostic")`
  at entry, including unwritten invariants such as parallel containers that
  must agree in length.
- Shared helper whose violated precondition is UB (div-by-zero, OOB) →
  the helper asserts its own precondition; a low-level helper called from
  many modules *is* a system boundary, and UB is platform-divergent.
- Helper reachable from both `constexpr` and runtime → `if consteval`
  branch that `static_assert`s, runtime `IR_ASSERT` otherwise.
- Precondition coupling module-level constants → `static_assert` beside the
  constants, in exact integer arithmetic, asserting the structural identity
  (`bias == kDepthEncodeShift`, not `bias == 8`); a shader-side constant is
  mirrored into the C++ constants header and asserted there
  (`ir_render_types.hpp` is the precedent). A constant that gains a second
  requirement later asserts it at the definition site.
- Value crossing an untrusted boundary (Lua, deserialized file, network) →
  clamp or reject at the boundary; `IR_RELEASE` strips asserts. Adding an
  `IR_ASSERT` to a type a Lua binding constructs, or a binding to an
  asserting setter, means checking the binding surface either way. Log a
  warning on the clamped path when the call is not per-frame.

Which assert: runtime → `IR_ASSERT(predicate, "diagnostic")` (engine log
sink, stripped under `IR_RELEASE`); compile-time → raw `static_assert` with
a message (there is no `IR_STATIC_ASSERT`); raw `<cassert>` `assert()`
never; thread affinity → `IR_ASSERT_MAIN_THREAD()`.

An invariant no automated run exercises (run-mode flag combinations,
hand-paired table indices, cross-file constraints on a field) gets a
load-bearing `static_assert` or startup assert, proven by restoring the bad
value and watching it fail; a test asserting an exclusion, and the matcher
(regex, glob set, allowlist) carrying it, owe the same proof — the excluded
token sits where the matcher scans and the test goes red when the exclusion
is deleted (VALIDATION.md "Positive control"). A comment naming a runtime
guard ("trips the `x < n` range assert") is read as fact: the guard exists
on every path the comment's scope implies, and since `IR_ASSERT` is
release-stripped, the comment says which build config. A guard, clamp, or
asymmetry whose rationale lives in another file says so at its own site, and
a declaration-order invariant lists its consumers at the declaration
(`.claude/skills/simplify/SKILL.md` §8 keeps `simplify` from reading such a
guard as deletable hygiene).

---

## Deprecation markers

When an API, file format, helper, or module surface is being replaced and
the replacement has shipped or has a concrete landing plan, mark it in three
places: `// DEPRECATED — use <X> instead.` above the declaration; a
`## Deprecated` section in the module's `CLAUDE.md` naming the surface, the
replacement, and the marking PR or issue; and a flag in any new design doc
that proposes to extend the surface. A task that proposes to extend a
deprecated surface escalates to the human (the usual answer is to redirect
to the replacement). Removal of ECS surface still needs human sign-off per
the Engine API removal rule; deprecation is the slower "going away" signal.

Transition shims — code intentionally dead once a known milestone lands
(compatibility aliases, legacy-artifact cleaners, deprecated-knob
fallthrough) — carry `// Remove once <phase> lands (epic #N)` at the
declaration so the cleanup pass is self-guided.

---

## Hard rules for autonomous fleet roles

These apply to every fleet role; a role file lists only its additional
restrictions.

- **Never `git push origin master`, never `--force` push, never `gh pr
  merge`.** The human merges — there is no auto-merge lane anywhere in the
  fleet (FLEET.md §"Who merges").
- **Never run `cmake --preset`** — only `cmake --build` (via `fleet-build`)
  against the configured tree.
- **Never touch the `.claude/worktrees/` layout.**
- **Never `sudo`, `brew install/upgrade/uninstall`, `apt`, or
  `xcode-select`.**
- **Never leave dirty edits uncommitted at the end of an iteration** — any
  change to the tree (manual, from `simplify`, from `optimize`) lands via
  `commit-and-push`; the next iteration's branch switch discards it. Don't
  invoke `simplify` standalone; `commit-and-push` runs it.
- **`.fleet/status/*.md` is scout-maintained bookkeeping** — read when a
  `CLAUDE.md` pointer directs you there; never in a feature PR's diff
  (`.fleet/status/README.md`).
- **Edit/Write paths stay inside your worktree.** The parent clone
  `/Users/evinjkill/src/IrredenEngine/` shares the tree shape, so an edit
  aimed there succeeds silently while your build sees nothing. Prefer
  relative paths; an absolute path starts with
  `/Users/evinjkill/src/IrredenEngine/.claude/worktrees/<your-basename>/`.
- **Mutating git flows assert their worktree first.** `commit-and-push`,
  `start-next-task`, and `fleet-pr-checkout-detached` run
  `fleet-assert-worktree` (exit 1 outside a `.claude/worktrees/*` tree);
  `cd` into your worktree and retry. Only a human in a deliberate
  main-clone session sets `FLEET_ALLOW_MAIN_CLONE=1`.
- **`refs/stash` is shared across all worktrees — never use positional
  stash refs.** A bare `git stash pop` / `drop` / `stash@{N}` races a
  parallel agent's entry. If a flow must stash: `git stash push -u -m
  "<worktree-unique tag>"`, resolve the entry by message, re-apply by
  commit SHA (`git stash apply <SHA>`), drop by re-resolving its current
  `stash@{N}`. `git stash create` avoids the shared stack but skips
  untracked files (stage them first); a temp-branch commit or a dedicated
  worktree also works. Reference pattern:
  [`.claude/skills/attach-screenshots/SKILL.md`](../../.claude/skills/attach-screenshots/SKILL.md).
- **Single-command Bash only** — [`## Bash tool rules`](#bash-tool-rules).
- **Edit/Write blocked for `.claude/commands/` files?** The harness gate
  blocks these paths even with `Edit(*)` / `Write(*)` allowlisted; use
  python3 (sanctioned via `Bash(python3:*)`):
  `python3 -c "f=open(path).read(); assert old in f, 'string not found'; open(path, 'w').write(f.replace(old, new, 1))"`
