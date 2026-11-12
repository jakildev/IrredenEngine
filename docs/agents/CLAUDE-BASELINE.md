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

Prefix, suffix and include-guard conventions for C++ members, components,
shaders, globals and header helpers. Read before editing source code:
[`CODE-CONVENTIONS.md` § "Naming"](CODE-CONVENTIONS.md#naming).

---

## Style

Early returns, ownership, `enum class`, data-only components, `IRMath`, no
header globals, comments that explain and never narrate, in C++ and Python.
Read before editing source code: [`CODE-CONVENTIONS.md` § "Style"](CODE-CONVENTIONS.md#style).

---

## What belongs in agent-facing docs

Point, don't dump: state each rule once in its canonical home and link to it.
Read before editing any instruction file (`CLAUDE.md`, `SKILL.md`, a role,
rule or protocol): [`INSTRUCTION-AUTHORING.md`](INSTRUCTION-AUTHORING.md#what-belongs-in-agent-facing-docs).

---

## Canonical-home map

| Topic | Canonical home |
|---|---|
| Bash rules · Cross-repo isolation · Engine API removal · Hard rules for fleet roles · Citing source in filed artifacts | this doc (`docs/agents/CLAUDE-BASELINE.md`) |
| Naming · Style · Encode contracts in code · Deprecation markers (read before editing source) | `docs/agents/CODE-CONVENTIONS.md` |
| What belongs in agent-facing docs (read before editing an instruction file) | `docs/agents/INSTRUCTION-AUTHORING.md` |
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
| Engine executables launch only through `ir-run` / `fleet-run` (machine-checkable) | `.claude/rules/engine-process-launches.md` |
| Running a rules detector tree-wide · the `creations/` sweep trap | `.claude/rules/README.md` |
| Tick-function signatures · INPUT → UPDATE → RENDER ordering | `engine/system/CLAUDE.md` |
| Component-method tier rules | `engine/prefabs/CLAUDE.md` |
| Asset serialization version-bump | `engine/asset/CLAUDE.md` |
| `--auto-screenshot` contract | `engine/video/CLAUDE.md` |
| PR-body templates · post-open provenance and host stamps | `.claude/skills/commit-and-push/procedures/` |

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

The allowlist matches a command prefix and Claude Code adds hardcoded
gates; each form below prompts or misbehaves under unattended operation.

- **One simple command per Bash call.** No `&&`, `||`, `;`, or `|`: run
  `cmd1`, read its output, then run `cmd2`. Prefer Read/Glob/Grep over Bash
  for files and directories — they return empty instead of exiting non-zero.
- **Don't parallel-batch fallible Bash calls.** A non-zero exit cancels every
  sibling in the batch (`Cancelled: parallel tool call … errored`). Batch
  only calls that certainly exit 0; issue probes (`which`, `ls`/`cat` on a
  maybe-absent path, a `gh` lookup that may 404, a build) one per turn.
- **No `cd <path> && git …`** — use `git -C <path> …`; the compound form
  may need an approval an unattended pane cannot give; whether it does
  differs by host and permission mode.
- **No `cmd > file`** — the redirect may need an approval an unattended pane
  cannot give; whether it does differs by host and permission mode. Read stdout
  from the tool result; write files with the **Write** tool.
- **No `sed -n 'N,Mp' file`** — Read with `offset` / `limit`.
- **No `git show ref:file | sed/head/tail`** — run `git show ref:file` alone.
- **No `cat file || echo fallback`** — Read the file.
- **Grep tool** instead of `grep`, **Glob** instead of `find`, `--jq` on
  `gh` instead of piping to `jq` / `python3`
  (`gh pr list --json number,title --jq '.[] | "#\(.number) \(.title)"'`).
- **Never root a tree search at `creations/` or `.claude/`.** Both own an
  ignore-then-negate block in `.gitignore`; ripgrep's walker (which also
  backs the Grep tool) prunes the children and reports a clean pass over
  a handful of the several hundred tracked files. Root at the repo top or a child (`creations/demos`), or
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

A documented precondition is enforced with `IR_ASSERT` / `static_assert`,
not by the comment alone. Read before editing source code:
[`CODE-CONVENTIONS.md`](CODE-CONVENTIONS.md#encode-contracts-in-code-not-in-comments).

---

## Deprecation markers

A surface being replaced is marked at its declaration, in its module's
`CLAUDE.md` and in any design doc extending it. Read before replacing or
extending an API: [`CODE-CONVENTIONS.md` § "Deprecation markers"](CODE-CONVENTIONS.md#deprecation-markers).

---

## Hard rules for autonomous fleet roles

These apply to every fleet role; a role file lists only its additional
restrictions.

- **Never `git push origin master`, never `--force` push, never `gh pr
  merge`, never `fleet-automerge`.** No LLM role merges. The human merges,
  and the tier-0 `fleet-automerge` script, run only by the dispatcher under
  the human-owned enablement, merges the classes FLEET.md §"Who merges"
  names. Its read-only partition, `fleet-merge-preview`, is allowed.
- **Never configure or reconfigure a worktree's `build/` by hand** —
  `fleet-build` configures it on first use. The one manual configure a fleet
  role runs is the dedicated downstream build directory in `BUILD.md`.
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
  `<engine-clone>/` shares the tree shape, so an edit
  aimed there succeeds silently while your build sees nothing. Prefer
  relative paths; an absolute path starts with
  `<engine-clone>/.claude/worktrees/<your-basename>/`.
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
- **Gated self-config is not editable by dispatched roles.** The definition
  and handling live in [`FLEET.md` § "Editing `.claude/` paths in headless
  mode"](FLEET.md#editing-claude-paths-in-headless-mode-fleet-edit).
